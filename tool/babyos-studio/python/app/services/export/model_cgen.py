"""模型 C 发射器（design §7.2/§7.5）：sklearn / XGBoost / LightGBM → static const C 数组 + predict。

统一约定（与 reference.py 逐操作对齐的前提）：
- 公共原语（argmax/softmax/dot/normalize/relu/sigmoid/树遍历）全部调用 BabyOS
  预置 algo_ml 模块（bos/algorithm/algo_ml.h，FR-10）；本文件只发射权重表 + 薄 predict 逻辑
- 归一化烘焙在 predict 入口：bAlgoMlNormalize（f32）
- 树判定：(double)xf <= threshold（f64 阈值烘焙，彻底避开 FMA/exp 差异）
- softmax 先减 max；argmax 严格 > 扫描（并列取最小索引）
- 向量点积/末级累加：bAlgoMlDot / bAlgoMlSoftmax（f32 顺序累加，
  C 侧 -ffp-contract=off 禁 FMA 收缩）
- GBDT（xgb/lgbm/xgb_r/lgbm_r）：
  - 树表复用 bAlgoMlTreePredict + 单 float 叶值表
  - XGB 判定 x < thr → 提取期 threshold = nextafter(thr, -np.inf)
  - XGB 二分类 config base_score 为概率空间 → margin 用 logit(base_score)
  - LGBM categorical（decision_type!='<='）导出期 ValueError 拒绝
"""
from __future__ import annotations

import re

import numpy as np

#: 树遍历深度契约：与 bos/algorithm/inc/algo_ml.h 的 ALGO_ML_TREE_MAX_DEPTH 保持一致，
#: 发射前断言（超限导出期 422 TREE_TOO_DEEP，design §7.5）
ALGO_ML_TREE_MAX_DEPTH = 128


def _f32(v: float) -> str:
    """float 常量字面量：保证被 C 解析为 float（加 F 后缀）。"""
    return np.format_float_scientific(np.float32(v), unique=True, trim="-") + "F"


def _f64(v: float) -> str:
    return np.format_float_scientific(np.float64(v), unique=True, trim="-")


def _arr_f32(a: np.ndarray, per_line: int = 6) -> str:
    flat = np.asarray(a, dtype=np.float32).ravel()
    items = [_f32(v) for v in flat]
    lines = []
    for i in range(0, len(items), per_line):
        lines.append("    " + ", ".join(items[i : i + per_line]))
    return ",\n".join(lines)


def _arr2_f32(a: np.ndarray, per_line: int = 6) -> str:
    """二维数组：每行内层花括号（-Werror=missing-braces 合规）。"""
    a = np.asarray(a, dtype=np.float32)
    rows = []
    for r in range(a.shape[0]):
        items = [_f32(v) for v in a[r]]
        inner = []
        for i in range(0, len(items), per_line):
            inner.append("        " + ", ".join(items[i : i + per_line]))
        rows.append("    {\n" + ",\n".join(inner) + "\n    }")
    return ",\n".join(rows)


def _arr_f64(a: np.ndarray, per_line: int = 4) -> str:
    flat = np.asarray(a, dtype=np.float64).ravel()
    items = [_f64(v) for v in flat]
    lines = []
    for i in range(0, len(items), per_line):
        lines.append("    " + ", ".join(items[i : i + per_line]))
    return ",\n".join(lines)


def _arr_i(a: np.ndarray, per_line: int = 12) -> str:
    flat = np.asarray(a).ravel()
    items = [str(int(v)) for v in flat]
    lines = []
    for i in range(0, len(items), per_line):
        lines.append("    " + ", ".join(items[i : i + per_line]))
    return ",\n".join(lines)


def extract_arrays(payload: dict, nc: int | None = None) -> dict:
    """从 joblib payload 提取各模型 C 数组（纯 numpy，发射器与参考实现共用同一份）。

    nc: 完整类别数（= len(payload['labels'])）。传入时树 proba 自动 pad 到 nc 列，
    避免训练 fold 缺类导致维度不匹配。
    """
    est = payload["estimator"]
    mt = payload["model_type"]
    out: dict = {"model_type": mt}
    if mt == "dt":
        out["trees"] = [_tree_arrays(est, nc)]
    elif mt in ("rf", "et"):
        out["trees"] = [_tree_arrays(e, nc) for e in est.estimators_]
        out["n_estimators"] = len(est.estimators_)
    elif mt == "lr":
        coef = np.asarray(est.coef_, dtype=np.float64)
        intercept = np.asarray(est.intercept_, dtype=np.float64)
        out["binary"] = coef.shape[0] == 1
        out["w"] = coef
        out["b"] = intercept
    elif mt == "nb":
        var = np.asarray(est.var_, dtype=np.float64)
        theta = np.asarray(est.theta_, dtype=np.float64)
        eps = 1e-9 * var.max()  # sklearn GaussianNB._check_epsilon 口径
        var = var + eps
        out["theta"] = theta
        out["nb_var"] = var
        out["nb_c0"] = -0.5 * np.log(2.0 * np.pi * var)
        out["nb_c1"] = -0.5 / var
        out["prior"] = np.log(np.asarray(est.class_prior_, dtype=np.float64))
    elif mt == "mlp":
        # sklearn coefs_[0]:(nf,h) coefs_[1]:(h,nc) → C 布局 [h][nf] / [nc][h]
        out["w1"] = np.asarray(est.coefs_[0], dtype=np.float64).T
        out["b1"] = np.asarray(est.intercepts_[0], dtype=np.float64)
        out["w2"] = np.asarray(est.coefs_[1], dtype=np.float64).T
        out["b2"] = np.asarray(est.intercepts_[1], dtype=np.float64)
    elif mt == "simple_nn":
        # SimpleNNClassifier: W1_, b1_, W2_, b2_ 已经是 [h][nf] / [nc][h] 布局
        out["w1"] = np.asarray(est.W1_, dtype=np.float64)
        out["b1"] = np.asarray(est.b1_, dtype=np.float64)
        out["w2"] = np.asarray(est.W2_, dtype=np.float64)
        out["b2"] = np.asarray(est.b2_, dtype=np.float64)
    elif mt in ("xgb", "lgbm"):
        out.update(_gbdt_classify_arrays(est, mt, nc))
    # ---------- 回归模型 ----------
    elif mt == "dt_r":
        out["trees"] = [_tree_arrays_regressor(est)]
        out["is_regressor"] = True
    elif mt in ("rf_r", "et_r"):
        out["trees"] = [_tree_arrays_regressor(e) for e in est.estimators_]
        out["n_estimators"] = len(est.estimators_)
        out["is_regressor"] = True
    elif mt == "lr_r":
        coef = np.asarray(est.coef_, dtype=np.float64)
        intercept = np.asarray(est.intercept_, dtype=np.float64)
        out["w"] = coef.reshape(1, -1) if coef.ndim == 1 else coef
        out["b"] = intercept.reshape(1) if intercept.ndim == 0 else intercept
        out["is_regressor"] = True
    elif mt == "simple_nn_r":
        out["w1"] = np.asarray(est.W1_, dtype=np.float64)
        out["b1"] = np.asarray(est.b1_, dtype=np.float64)
        out["w2"] = np.asarray(est.W2_, dtype=np.float64)
        out["b2"] = np.asarray(est.b2_, dtype=np.float64)
        out["is_regressor"] = True
    elif mt in ("xgb_r", "lgbm_r"):
        out.update(_gbdt_regress_arrays(est, mt))
        out["is_regressor"] = True
    else:
        raise ValueError(f"未知模型类型: {mt}")
    return out


def _tree_arrays(tree, nc: int | None = None) -> dict:
    t = tree.tree_
    proba = np.asarray(t.value, dtype=np.float64)[:, 0, :]
    sums = proba.sum(axis=1, keepdims=True)
    sums[sums == 0] = 1.0
    proba = proba / sums
    # 当训练 fold 缺少部分类别时，proba 列数 < nc；
    # 用 tree.classes_ 映射到完整类别空间，缺失列补 0。
    if nc is not None and proba.shape[1] < nc:
        est_classes = getattr(tree, "classes_", None)
        if est_classes is not None:
            cls_to_idx = {int(c): i for i, c in enumerate(est_classes)}
            padded = np.zeros((proba.shape[0], nc), dtype=np.float64)
            for target_idx in range(nc):
                source_idx = cls_to_idx.get(target_idx, -1)
                if 0 <= source_idx < proba.shape[1]:
                    padded[:, target_idx] = proba[:, source_idx]
            proba = padded
    return {
        "feat": np.asarray(t.feature),
        "thr": np.asarray(t.threshold, dtype=np.float64),
        "left": np.asarray(t.children_left),
        "right": np.asarray(t.children_right),
        "proba": proba,
        "n_nodes": t.node_count,
    }


def _tree_arrays_regressor(tree) -> dict:
    """回归树：叶节点存预测值（单个 float），非概率分布。"""
    t = tree.tree_
    # value shape: (n_nodes, 1, 1) → squeeze to (n_nodes,)
    values = np.asarray(t.value, dtype=np.float64).ravel()
    return {
        "feat": np.asarray(t.feature),
        "thr": np.asarray(t.threshold, dtype=np.float64),
        "left": np.asarray(t.children_left),
        "right": np.asarray(t.children_right),
        "values": values,
        "n_nodes": t.node_count,
    }


# ---------- GBDT（XGBoost / LightGBM）解析 ----------

_XGB_SPLIT_RE = re.compile(r"^(\d+):\[f(\d+)<([^\]]+)\]\s+yes=(\d+),no=(\d+)")
_XGB_LEAF_RE = re.compile(r"^(\d+):leaf=([^,]+)")


def _nodes_depth(nodes: list[dict]) -> int:
    """由扁平节点表计算树深（根=0；单叶树深度 0）。BFS，避免深树递归爆栈。"""
    if not nodes:
        return 0
    cur = [0]
    depth = 0
    while cur:
        nxt = []
        for i in cur:
            nd = nodes[i]
            if nd["feat"] < 0:
                continue
            depth += 1
            if nd["left"] >= 0:
                nxt.append(nd["left"])
            if nd["right"] >= 0:
                nxt.append(nd["right"])
        cur = nxt
    return depth


def _parse_xgb_tree(dump_str: str) -> list[dict]:
    """解析 XGBoost get_dump(with_stats=True) 单棵树。

    节点: {feat, left, right, thr, val}
    - yes → left, no → right（XGB: x < thr 走 yes）
    - threshold 烘焙为 nextafter(thr, -inf)，使 C 侧 (double)x <= thr_c
      与 XGB 的 x < thr 对任意 float x 等价
    - missing 列在提取期忽略（MCU 无 NaN 输入）
    """
    raw: dict[int, dict] = {}
    for line in dump_str.splitlines():
        s = line.strip()
        if not s:
            continue
        m = _XGB_SPLIT_RE.match(s)
        if m:
            nid = int(m.group(1))
            feat = int(m.group(2))
            thr_f = np.float32(m.group(3))
            # x < thr_f  ⟺  (double)x <= nextafter(thr_f, -inf)
            thr_c = float(np.nextafter(np.float64(thr_f), -np.inf))
            raw[nid] = {
                "feat": feat,
                "left": int(m.group(4)),
                "right": int(m.group(5)),
                "thr": thr_c,
                "val": 0.0,
            }
            continue
        m = _XGB_LEAF_RE.match(s)
        if m:
            nid = int(m.group(1))
            raw[nid] = {
                "feat": -1,
                "left": -1,
                "right": -1,
                "thr": 0.0,
                "val": float(m.group(2)),
            }
            continue
        raise ValueError(f"XGBoost tree dump 无法解析节点: {s!r}")
    if not raw:
        raise ValueError("XGBoost tree dump 为空")
    n = max(raw.keys()) + 1
    nodes = []
    for i in range(n):
        if i not in raw:
            raise ValueError(f"XGBoost tree dump 节点索引不连续，缺失节点 {i}")
        nd = raw[i]
        if nd["feat"] >= 0:
            for child in (nd["left"], nd["right"]):
                if child < 0 or child >= n:
                    raise ValueError(
                        f"XGBoost tree dump 节点 {i} 子节点索引非法: {child}"
                    )
        nodes.append(nd)
    return nodes


def _parse_lgbm_tree(node: dict, nodes: list[dict], depth: int = 0) -> int:
    """递归展平 LightGBM tree_structure。返回该节点在 nodes 中的下标。"""
    if depth > ALGO_ML_TREE_MAX_DEPTH:
        raise ValueError(
            f"TREE_TOO_DEEP: LightGBM 树深度超过 ALGO_ML_TREE_MAX_DEPTH({ALGO_ML_TREE_MAX_DEPTH})"
        )
    idx = len(nodes)
    if "split_feature" in node:
        dt = str(node.get("decision_type", "")).strip()
        num_cat = int(node.get("num_cat", 0) or 0)
        if dt != "<=" or num_cat > 0:
            raise ValueError(
                f"LightGBM categorical 特征不支持 C 导出（decision_type={dt!r}, "
                f"num_cat={num_cat}）。请改用数值特征重新训练后再导出。"
            )
        nodes.append(
            {
                "feat": int(node["split_feature"]),
                "left": -1,
                "right": -1,
                "thr": float(node["threshold"]),
                "val": 0.0,
            }
        )
        left_i = _parse_lgbm_tree(node["left_child"], nodes, depth + 1)
        right_i = _parse_lgbm_tree(node["right_child"], nodes, depth + 1)
        nodes[idx]["left"] = left_i
        nodes[idx]["right"] = right_i
    else:
        nodes.append(
            {
                "feat": -1,
                "left": -1,
                "right": -1,
                "thr": 0.0,
                "val": float(node.get("leaf_value", 0.0)),
            }
        )
    return idx


def _pack_trees(nodes_list: list[list[dict]]) -> list[dict]:
    """扁平节点表 → 与 sklearn _tree_arrays 同构的 trees[]（叶存 values）。"""
    trees = []
    for nodes in nodes_list:
        n = len(nodes)
        feat = np.array([nd["feat"] for nd in nodes], dtype=np.int64)
        left = np.array([nd["left"] for nd in nodes], dtype=np.int64)
        right = np.array([nd["right"] for nd in nodes], dtype=np.int64)
        thr = np.array([nd["thr"] for nd in nodes], dtype=np.float64)
        values = np.array([nd["val"] for nd in nodes], dtype=np.float64)
        trees.append(
            {
                "feat": feat,
                "thr": thr,
                "left": left,
                "right": right,
                "values": values,
                "n_nodes": n,
            }
        )
    return trees


def _check_gbd_depth(trees: list[dict]) -> None:
    for i, tr in enumerate(trees):
        d = _nodes_depth(
            [
                {
                    "feat": int(tr["feat"][r]),
                    "left": int(tr["left"][r]),
                    "right": int(tr["right"][r]),
                }
                for r in range(tr["n_nodes"])
            ]
        )
        if d > ALGO_ML_TREE_MAX_DEPTH:
            raise ValueError(
                f"TREE_TOO_DEEP: 第 {i} 棵 GBDT 树深度 {d} 超过 "
                f"ALGO_ML_TREE_MAX_DEPTH({ALGO_ML_TREE_MAX_DEPTH})"
            )


def _xgb_base_score(booster) -> float:
    """从 booster.save_config() 解析 base_score（如 "5E-1" → 0.5）。"""
    cfg = booster.save_config()
    m = re.search(r'"base_score"\s*:\s*"([^"]+)"', cfg)
    if not m:
        return 0.5
    return float(m.group(1))


def _xgb_binary_margin_base(booster) -> float:
    """XGB 二分类 margin 用的 base：config 中 base_score 为概率空间，需 logit。

    XGBoost 2.x boost_from_average 将 base_score 设为 y 均值（概率），
    实际 margin = logit(base_score) + Σ leaf。logit(0.5)=0。
    边界 p<=0 / p>=1 时退化为直接使用 config 值（防御）。
    """
    bs = _xgb_base_score(booster)
    if bs <= 0.0 or bs >= 1.0:
        return bs
    return float(np.log(bs / (1.0 - bs)))


def _gbdt_classify_arrays(est, mt: str, nc: int) -> dict:
    """XGB/LGBM 分类：树表 + 每树所属类 + base_score。"""
    if mt == "xgb":
        booster = est.get_booster()
        dumps = booster.get_dump(with_stats=True)
        nodes_list = [_parse_xgb_tree(d) for d in dumps]
        n_classes_est = int(getattr(est, "n_classes_", nc) or nc)
        # 二分类 XGB：每轮 1 棵树（raw[0]）；多分类：每轮 nc 棵
        is_binary = n_classes_est <= 2
        tree_nc = 1 if is_binary else max(nc, 1)
        if is_binary:
            # 二分类：base_score 为概率空间 → logit 后进 margin（与 XGB predict 对齐）
            base_val = _xgb_binary_margin_base(booster)
        else:
            # 多分类：config base_score 直接加到各类 raw（softmax 下常数不变）
            base_val = _xgb_base_score(booster)
        bases = np.full(tree_nc, base_val, dtype=np.float64)
        average_output = False
        objective = "binary:logistic" if is_binary else "multi:softprob"
    else:
        lm = est.booster_.dump_model()
        nodes_list = []
        for tree in lm["tree_info"]:
            nodes: list[dict] = []
            _parse_lgbm_tree(tree["tree_structure"], nodes)
            nodes_list.append(nodes)
        objective = str(lm.get("objective") or "")
        num_class = int(lm.get("num_class") or 1)
        is_multiclass = "multiclass" in objective or num_class > 1
        is_binary = (not is_multiclass) and ("sigmoid" in objective or nc == 2)
        # LGBM 无 base_score（已含在叶值/init）
        if is_multiclass:
            tree_nc = max(num_class, nc, 1)
        else:
            tree_nc = 1
        bases = np.zeros(tree_nc, dtype=np.float64)
        average_output = bool(lm.get("average_output", False))
        if average_output:
            raise ValueError(
                "LightGBM average_output=True 暂不支持 C 导出，请关闭后重训。"
            )
    trees = _pack_trees(nodes_list)
    _check_gbd_depth(trees)
    return {
        "trees": trees,
        "n_estimators": len(trees),
        "tree_nc": tree_nc,
        "base": bases,
        "is_binary": is_binary,
        "backend": "xgb" if mt == "xgb" else "lgbm",
        "objective": objective,
        "average_output": average_output,
    }


def _gbdt_regress_arrays(est, mt: str) -> dict:
    """XGB/LGBM 回归：全部叶值求和（XGB 另加 base_score）。"""
    if mt == "xgb_r":
        booster = est.get_booster()
        dumps = booster.get_dump(with_stats=True)
        nodes_list = [_parse_xgb_tree(d) for d in dumps]
        base = np.array([_xgb_base_score(booster)], dtype=np.float64)
        average_output = False
    else:
        lm = est.booster_.dump_model()
        nodes_list = []
        for tree in lm["tree_info"]:
            nodes: list[dict] = []
            _parse_lgbm_tree(tree["tree_structure"], nodes)
            nodes_list.append(nodes)
        base = np.zeros(1, dtype=np.float64)
        average_output = bool(lm.get("average_output", False))
        if average_output:
            raise ValueError(
                "LightGBM average_output=True 暂不支持 C 导出，请关闭后重训。"
            )
    trees = _pack_trees(nodes_list)
    _check_gbd_depth(trees)
    return {
        "trees": trees,
        "n_estimators": len(trees),
        "tree_nc": 1,
        "base": base,
        "backend": "xgb" if mt == "xgb_r" else "lgbm",
        "average_output": average_output,
        "is_regressor": True,
    }


# ---------- 树（DT/RF/ET：bAlgoMlTreeNode_t 节点表 + 独立叶概率表，布局契约见 algo_ml.h） ----------


def _check_depth(payload: dict, estimators) -> None:
    """深度契约（design §7.5）：任一树深度超 ALGO_ML_TREE_MAX_DEPTH → 导出期拒绝，
    不许固件运行时越界。由 export_service 转为 422 TREE_TOO_DEEP。"""
    if hasattr(estimators, "estimators_"):
        estimators = estimators.estimators_
    depth = max(int(e.get_depth()) for e in estimators)
    if depth > ALGO_ML_TREE_MAX_DEPTH:
        raise ValueError(
            f"TREE_TOO_DEEP: 树深度 {depth} 超过 ALGO_ML_TREE_MAX_DEPTH({ALGO_ML_TREE_MAX_DEPTH})"
        )


def _emit_tree_decls(trees: list[dict], prefix: str, nc: int) -> str:
    """每树两份表：bAlgoMlTreeNode_t 节点表（节点序 = sklearn 原始节点序）+
    平铺叶概率表（proba_off = 按节点索引升序的叶序号 × nc，行为主序）。"""
    decls = []
    for i, tr in enumerate(trees):
        n = tr["n_nodes"]
        leaf_ord: dict[int, int] = {}
        ord_i = 0
        for r in range(n):
            if tr["feat"][r] < 0:
                leaf_ord[r] = ord_i
                ord_i += 1
        rows = []
        for r in range(n):
            po = leaf_ord[r] if tr["feat"][r] < 0 else -1
            rows.append(
                "    { " + f"{int(tr['feat'][r])}, {int(tr['left'][r])}, {int(tr['right'][r])}, {po}, "
                + _f64(tr["thr"][r]) + " }"
            )
        proba_rows = [
            "    " + ", ".join(_f32(v) for v in tr["proba"][r])
            for r in range(n)
            if tr["feat"][r] < 0
        ]
        decls.append(
            f"static const bAlgoMlTreeNode_t {prefix}_nodes_t{i}[{n}] = {{\n"
            + ",\n".join(rows)
            + "\n};"
        )
        decls.append(
            f"static const float {prefix}_proba_t{i}[{ord_i} * {nc}] = {{\n"
            + ",\n".join(proba_rows)
            + "\n};"
        )
    return "\n\n".join(decls)


def _tree_proba_row(prefix: str, tree_var: str, proba_var: str, nnodes: str,
                    nc: int, nf: int, indent: str = "    ") -> str:
    """单树 proba 装载语句组：bAlgoMlTreePredict 取叶偏移 → 逐类拷贝。"""
    return (
        f"{indent}{{\n"
        f"{indent}    int off = bAlgoMlTreePredict({tree_var}, {nnodes}, xf, {nf});\n"
        f"{indent}    int k;\n"
        f"{indent}    for (k = 0; k < {nc}; k++) {{ out[k] = {proba_var}[(uint32_t)off * {nc} + k]; }}\n"
        f"{indent}}}"
    )


def emit_tree(payload: dict, arrs: dict, prefix: str, nc: int, nf: int,
              nclass_macro: str) -> dict:
    trees = arrs["trees"]
    _check_depth(payload, [payload["estimator"]])
    decls = _emit_tree_decls(trees, prefix, nc)
    body = _tree_proba_row(prefix, f"{prefix}_nodes_t0", f"{prefix}_proba_t0",
                           str(trees[0]["n_nodes"]), nc, nf)
    return {"decls": decls, "helpers": "", "predict_core": body, "needs_scratch": False}


def emit_forest(payload: dict, arrs: dict, prefix: str, nc: int, nf: int,
                nclass_macro: str) -> dict:
    trees = arrs["trees"]
    n_trees = len(trees)
    _check_depth(payload, payload["estimator"])
    decls = _emit_tree_decls(trees, prefix, nc)
    ptrs = ",\n".join(f"    {prefix}_nodes_t{i}" for i in range(n_trees))
    pptrs = ",\n".join(f"    {prefix}_proba_t{i}" for i in range(n_trees))
    nn = ", ".join(str(tr["n_nodes"]) for tr in trees)
    decls += (
        f"\n\nstatic const bAlgoMlTreeNode_t *const {prefix}_tree_ptrs[{n_trees}] = {{\n{ptrs}\n}};"
        f"\n\nstatic const float *const {prefix}_proba_ptrs[{n_trees}] = {{\n{pptrs}\n}};"
        f"\n\nstatic const uint16_t {prefix}_tree_nnodes[{n_trees}] = {{ {nn} }};"
    )
    core = f"""{{
    float tmp[{nclass_macro}];
    int   k, t, off;
    for (k = 0; k < {nc}; k++) {{ out[k] = 0.0f; }}
    for (t = 0; t < {n_trees}; t++)
    {{
        off = bAlgoMlTreePredict({prefix}_tree_ptrs[t], {prefix}_tree_nnodes[t], xf, {nf});
        for (k = 0; k < {nc}; k++) {{ tmp[k] = {prefix}_proba_ptrs[t][(uint32_t)off * {nc} + k]; }}
        for (k = 0; k < {nc}; k++) {{ out[k] = out[k] + tmp[k] / {float(n_trees)}F; }}
    }}
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": False}


def emit_lr(arrs: dict, prefix: str, nc: int, nf: int, nclass_macro: str) -> dict:
    if arrs["binary"]:
        decls = (
            f"static const float {prefix}_w[{nf}] = {{\n{_arr_f32(arrs['w'][0])}\n}};\n"
            f"static const float {prefix}_b = {_f32(arrs['b'][0])};"
        )
        core = f"""{{
    float z = {prefix}_b + bAlgoMlDot({prefix}_w, xf, {nf});
    /* 二分类 LR：sklearn 口径 proba[1] = sigmoid(z)，proba[0] = 1 - proba[1] */
    out[1] = bAlgoMlSigmoid(z);
    out[0] = 1.0f - out[1];
}}"""
    else:
        decls = (
            f"static const float {prefix}_w[{nc}][{nf}] = {{\n{_arr2_f32(arrs['w'])}\n}};\n"
            f"static const float {prefix}_b[{nc}] = {{\n{_arr_f32(arrs['b'])}\n}};"
        )
        core = f"""{{
    float z[{nclass_macro}];
    int k;
    for (k = 0; k < {nc}; k++)
    {{
        z[k] = {prefix}_b[k] + bAlgoMlDot({prefix}_w[k], xf, {nf});
    }}
    bAlgoMlSoftmax(out, z, {nc});
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": True}


def emit_nb(arrs: dict, prefix: str, nc: int, nf: int, nclass_macro: str) -> dict:
    decls = (
        f"static const float {prefix}_theta[{nc}][{nf}] = {{\n{_arr2_f32(arrs['theta'])}\n}};\n"
        f"static const float {prefix}_c0[{nc}][{nf}] = {{\n{_arr2_f32(arrs['nb_c0'])}\n}};\n"
        f"static const float {prefix}_c1[{nc}][{nf}] = {{\n{_arr2_f32(arrs['nb_c1'])}\n}};\n"
        f"static const float {prefix}_prior[{nc}] = {{\n{_arr_f32(arrs['prior'])}\n}};"
    )
    core = f"""{{
    float z[{nclass_macro}];
    int k, j;
    for (k = 0; k < {nc}; k++)
    {{
        float acc = {prefix}_prior[k];
        for (j = 0; j < {nf}; j++)
        {{
            float d = xf[j] - {prefix}_theta[k][j];
            acc = acc + ({prefix}_c0[k][j] + {prefix}_c1[k][j] * (d * d));
        }}
        z[k] = acc;
    }}
    bAlgoMlSoftmax(out, z, {nc});
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": True}


def emit_mlp(arrs: dict, prefix: str, nc: int, nf: int, nclass_macro: str) -> dict:
    h = arrs["w1"].shape[0]
    n_out = arrs["w2"].shape[0]  # 1 for binary, nc for multiclass
    decls = (
        f"static const float {prefix}_w1[{h}][{nf}] = {{\n{_arr2_f32(arrs['w1'])}\n}};\n"
        f"static const float {prefix}_b1[{h}] = {{\n{_arr_f32(arrs['b1'])}\n}};\n"
        f"static const float {prefix}_w2[{n_out}][{h}] = {{\n{_arr2_f32(arrs['w2'])}\n}};\n"
        f"static const float {prefix}_b2[{n_out}] = {{\n{_arr_f32(arrs['b2'])}\n}};"
    )
    if n_out == 1:
        # 二分类 MLP：单输出 + sigmoid（与 LR binary 路径一致）
        core = f"""{{
    static float hidden[{h}];
    float z;
    int i;
    for (i = 0; i < {h}; i++)
    {{
        float zi = {prefix}_b1[i] + bAlgoMlDot({prefix}_w1[i], xf, {nf});
        hidden[i] = bAlgoMlRelu(zi);
    }}
    z = {prefix}_b2[0] + bAlgoMlDot({prefix}_w2[0], hidden, {h});
    /* 二分类 MLP：proba[1] = sigmoid(z), proba[0] = 1 - proba[1] */
    out[1] = bAlgoMlSigmoid(z);
    out[0] = 1.0f - out[1];
}}"""
    else:
        core = f"""{{
    static float hidden[{h}];   /* static 化：避开 R-INT-3 栈上 ≥64B 大数组红线（design P3-1） */
    float z[{nclass_macro}];
    int i, k;
    for (i = 0; i < {h}; i++)
    {{
        float zi = {prefix}_b1[i] + bAlgoMlDot({prefix}_w1[i], xf, {nf});
        hidden[i] = bAlgoMlRelu(zi);
    }}
    for (k = 0; k < {nc}; k++)
    {{
        z[k] = {prefix}_b2[k] + bAlgoMlDot({prefix}_w2[k], hidden, {h});
    }}
    bAlgoMlSoftmax(out, z, {nc});
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": True, "hidden": h}


# ---------- 回归模型发射器 ----------

def _emit_regressor_tree_decls(trees: list[dict], prefix: str) -> str:
    """回归树：节点表 + 叶值表（单 float，非概率分布）。"""
    decls = []
    for i, tr in enumerate(trees):
        n = tr["n_nodes"]
        leaf_ord: dict[int, int] = {}
        ord_i = 0
        for r in range(n):
            if tr["feat"][r] < 0:
                leaf_ord[r] = ord_i
                ord_i += 1
        rows = []
        for r in range(n):
            po = leaf_ord[r] if tr["feat"][r] < 0 else -1
            rows.append(
                "    { " + f"{int(tr['feat'][r])}, {int(tr['left'][r])}, {int(tr['right'][r])}, {po}, "
                + _f64(tr["thr"][r]) + " }"
            )
        # 叶值表：每个叶节点一个 float
        leaf_vals = [
            _f64(tr["values"][r])
            for r in range(n)
            if tr["feat"][r] < 0
        ]
        decls.append(
            f"static const bAlgoMlTreeNode_t {prefix}_nodes_t{i}[{n}] = {{\n"
            + ",\n".join(rows)
            + "\n};"
        )
        decls.append(
            f"static const float {prefix}_vals_t{i}[{ord_i}] = {{\n"
            + "    " + ", ".join(leaf_vals)
            + "\n};"
        )
    return "\n\n".join(decls)


def emit_regressor_tree(payload: dict, arrs: dict, prefix: str, nc: int, nf: int,
                         nclass_macro: str) -> dict:
    """回归决策树：单树遍历，返回叶节点预测值。"""
    trees = arrs["trees"]
    _check_depth(payload, [payload["estimator"]])
    decls = _emit_regressor_tree_decls(trees, prefix)
    body = (
        f"{{\n"
        f"    int off = bAlgoMlTreePredict({prefix}_nodes_t0, "
        f"{trees[0]['n_nodes']}, xf, {nf});\n"
        f"    *out = {prefix}_vals_t0[(uint32_t)off];\n"
        f"}}"
    )
    return {"decls": decls, "helpers": "", "predict_core": body, "needs_scratch": False}


def emit_regressor_forest(payload: dict, arrs: dict, prefix: str, nc: int, nf: int,
                           nclass_macro: str) -> dict:
    """回归随机森林/极端树：多树平均。"""
    trees = arrs["trees"]
    n_trees = len(trees)
    _check_depth(payload, payload["estimator"])
    decls = _emit_regressor_tree_decls(trees, prefix)
    ptrs = ",\n".join(f"    {prefix}_nodes_t{i}" for i in range(n_trees))
    vptrs = ",\n".join(f"    {prefix}_vals_t{i}" for i in range(n_trees))
    nn = ", ".join(str(tr["n_nodes"]) for tr in trees)
    decls += (
        f"\n\nstatic const bAlgoMlTreeNode_t *const {prefix}_tree_ptrs[{n_trees}] = {{\n{ptrs}\n}};"
        f"\n\nstatic const float *const {prefix}_vals_ptrs[{n_trees}] = {{\n{vptrs}\n}};"
        f"\n\nstatic const uint16_t {prefix}_tree_nnodes[{n_trees}] = {{ {nn} }};"
    )
    core = f"""{{
    float sum = 0.0f;
    int t, off;
    for (t = 0; t < {n_trees}; t++)
    {{
        off = bAlgoMlTreePredict({prefix}_tree_ptrs[t], {prefix}_tree_nnodes[t], xf, {nf});
        sum += {prefix}_vals_ptrs[t][(uint32_t)off];
    }}
    *out = sum / {float(n_trees)}F;
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": False}


def emit_regressor_lr(arrs: dict, prefix: str, nc: int, nf: int, nclass_macro: str) -> dict:
    """回归线性模型：y = w·x + b。"""
    w = arrs["w"]  # shape (1, nf) or (nf,)
    b = arrs["b"]  # shape (1,) or scalar
    w_flat = w.ravel()
    decls = (
        f"static const float {prefix}_w[{nf}] = {{\n{_arr_f32(w_flat)}\n}};\n"
        f"static const float {prefix}_b = {_f32(float(b.ravel()[0]))};"
    )
    core = f"""{{
    *out = {prefix}_b + bAlgoMlDot({prefix}_w, xf, {nf});
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": False}


def emit_regressor_mlp(arrs: dict, prefix: str, nc: int, nf: int, nclass_macro: str) -> dict:
    """回归 MLP/SimpleNN：单输出线性激活（无 softmax）。"""
    h = arrs["w1"].shape[0]
    decls = (
        f"static const float {prefix}_w1[{h}][{nf}] = {{\n{_arr2_f32(arrs['w1'])}\n}};\n"
        f"static const float {prefix}_b1[{h}] = {{\n{_arr_f32(arrs['b1'])}\n}};\n"
        f"static const float {prefix}_w2[{h}] = {{\n{_arr_f32(arrs['w2'].ravel())}\n}};\n"
        f"static const float {prefix}_b2 = {_f32(float(arrs['b2'].ravel()[0]))};"
    )
    core = f"""{{
    static float hidden[{h}];
    float z;
    int i;
    for (i = 0; i < {h}; i++)
    {{
        float zi = {prefix}_b1[i] + bAlgoMlDot({prefix}_w1[i], xf, {nf});
        hidden[i] = bAlgoMlRelu(zi);
    }}
    z = {prefix}_b2 + bAlgoMlDot({prefix}_w2, hidden, {h});
    *out = z;
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": True, "hidden": h}


# ---------- GBDT（XGBoost / LightGBM）发射器 ----------
# 树表复用 bAlgoMlTreePredict + 单 float 叶值表；proba_off = 叶序号。
# XGB: x < thr 走 left，thr 已在提取期做 nextafter(thr,-inf) 适配。
# LGBM: x <= thr 走 left，与 C 侧判定一致。


def _emit_gbdt_tree_decls(trees: list[dict], prefix: str) -> str:
    """GBDT 树：节点表 + 单 float 叶值表（与回归树布局相同）。"""
    return _emit_regressor_tree_decls(trees, prefix)


def _emit_gbdt_ptrs(trees: list[dict], prefix: str) -> str:
    n_trees = len(trees)
    ptrs = ",\n".join(f"    {prefix}_nodes_t{i}" for i in range(n_trees))
    vptrs = ",\n".join(f"    {prefix}_vals_t{i}" for i in range(n_trees))
    nn = ", ".join(str(tr["n_nodes"]) for tr in trees)
    return (
        f"\n\nstatic const bAlgoMlTreeNode_t *const {prefix}_tree_ptrs[{n_trees}] = {{\n{ptrs}\n}};"
        f"\n\nstatic const float *const {prefix}_vals_ptrs[{n_trees}] = {{\n{vptrs}\n}};"
        f"\n\nstatic const uint16_t {prefix}_tree_nnodes[{n_trees}] = {{ {nn} }};"
    )


def emit_gbdt_classifier(payload: dict, arrs: dict, prefix: str, nc: int, nf: int,
                         nclass_macro: str) -> dict:
    """XGB/LGBM 分类：raw[k] = base[k] + Σ_{t%tree_nc==k} leaf_t，再 sigmoid/softmax。

    二分类（tree_nc==1）：proba[1]=sigmoid(raw[0])，proba[0]=1-proba[1]。
    多分类：proba=softmax(raw)。
    LGBM 无 base_score（全 0）；XGB 各类 base 相同（= booster base_score）。
    """
    trees = arrs["trees"]
    n_trees = len(trees)
    tree_nc = int(arrs.get("tree_nc", 1))
    bases = np.asarray(arrs.get("base", np.zeros(tree_nc)), dtype=np.float64)
    is_binary = bool(arrs.get("is_binary", nc <= 2)) or tree_nc == 1
    decls = _emit_gbdt_tree_decls(trees, prefix) + _emit_gbdt_ptrs(trees, prefix)

    if is_binary:
        b0 = float(bases[0]) if bases.size else 0.0
        core = f"""{{
    float raw = {_f64(b0)};
    int t, off;
    for (t = 0; t < {n_trees}; t++)
    {{
        off = bAlgoMlTreePredict({prefix}_tree_ptrs[t], {prefix}_tree_nnodes[t], xf, {nf});
        raw += {prefix}_vals_ptrs[t][(uint32_t)off];
    }}
    /* 二分类 GBDT：proba[1] = sigmoid(raw)，proba[0] = 1 - proba[1] */
    out[1] = bAlgoMlSigmoid(raw);
    out[0] = 1.0f - out[1];
}}"""
        return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": False}

    # 多分类：raw[k] = base[k] + Σ_{t % tree_nc == k} leaf
    base_rows = ", ".join(_f64(float(bases[k])) if k < bases.size else "0.0" for k in range(nc))
    core = f"""{{
    float raw[{nclass_macro}];
    int k, t, off;
    static const float {prefix}_base[{nc}] = {{ {base_rows} }};
    for (k = 0; k < {nc}; k++) {{ raw[k] = {prefix}_base[k]; }}
    for (t = 0; t < {n_trees}; t++)
    {{
        off = bAlgoMlTreePredict({prefix}_tree_ptrs[t], {prefix}_tree_nnodes[t], xf, {nf});
        raw[t % {tree_nc}] += {prefix}_vals_ptrs[t][(uint32_t)off];
    }}
    bAlgoMlSoftmax(out, raw, {nc});
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": True}


def emit_gbdt_regressor(payload: dict, arrs: dict, prefix: str, nc: int, nf: int,
                        nclass_macro: str) -> dict:
    """XGB/LGBM 回归：*out = base + Σ all leaf（LGBM average_output=False 直接求和）。"""
    trees = arrs["trees"]
    n_trees = len(trees)
    base = float(np.asarray(arrs.get("base", np.zeros(1)), dtype=np.float64).ravel()[0])
    decls = _emit_gbdt_tree_decls(trees, prefix) + _emit_gbdt_ptrs(trees, prefix)
    core = f"""{{
    float sum = {_f64(base)};
    int t, off;
    for (t = 0; t < {n_trees}; t++)
    {{
        off = bAlgoMlTreePredict({prefix}_tree_ptrs[t], {prefix}_tree_nnodes[t], xf, {nf});
        sum += {prefix}_vals_ptrs[t][(uint32_t)off];
    }}
    *out = sum;
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": False}


def emit_simple_nn_binary(arrs: dict, prefix: str, nc: int, nf: int,
                          nclass_macro: str) -> dict:
    """SimpleNN 二分类（nc=2，W2_ 形状 (2, h)）：与 SimpleNNClassifier.predict_proba 对齐。

    SimpleNNClassifier._encode_labels 二分类返回 n_out=len(classes)=2（但 Y 为
    (n,1) 0/1 目标）；predict_proba 对双 logit 做 softmax 后返回
    [1 - softmax([z0,z1])[0], softmax([z0,z1])[0]]，即
    proba[1] = softmax([z0,z1])[0] = sigmoid(z0 - z1)。
    导出 C 必须复现该口径（不能走标准 nc 路 softmax，否则类别列颠倒）。
    """
    h = arrs["w1"].shape[0]
    decls = (
        f"static const float {prefix}_w1[{h}][{nf}] = {{\n{_arr2_f32(arrs['w1'])}\n}};\n"
        f"static const float {prefix}_b1[{h}] = {{\n{_arr_f32(arrs['b1'])}\n}};\n"
        f"static const float {prefix}_w2[2][{h}] = {{\n{_arr2_f32(arrs['w2'])}\n}};\n"
        f"static const float {prefix}_b2[2] = {{\n{_arr_f32(arrs['b2'])}\n}};"
    )
    core = f"""{{
    static float hidden[{h}];
    float z0, z1;
    int i;
    for (i = 0; i < {h}; i++)
    {{
        float zi = {prefix}_b1[i] + bAlgoMlDot({prefix}_w1[i], xf, {nf});
        hidden[i] = bAlgoMlRelu(zi);
    }}
    z0 = {prefix}_b2[0] + bAlgoMlDot({prefix}_w2[0], hidden, {h});
    z1 = {prefix}_b2[1] + bAlgoMlDot({prefix}_w2[1], hidden, {h});
    /* SimpleNN 二分类：proba[1] = sigmoid(z0 - z1) = softmax([z0,z1])[0]，
       与 SimpleNNClassifier.predict_proba 对齐；proba[0] = 1 - proba[1] */
    out[1] = bAlgoMlSigmoid(z0 - z1);
    out[0] = 1.0f - out[1];
}}"""
    return {"decls": decls, "helpers": "", "predict_core": core, "needs_scratch": True, "hidden": h}


def emit_model(payload: dict, arrs: dict, prefix: str, nc: int, nf: int,
               nclass_macro: str) -> dict:
    """分发到具体发射器。返回 {decls, helpers, predict_core, needs_scratch}。

    nclass_macro: 类别数烘焙宏名（ALGO_<NAME>_N_CLASSES），z 栈数组维度用它声明。
    """
    mt = arrs["model_type"]
    is_regressor = arrs.get("is_regressor", False)

    # ---------- 回归模型 ----------
    if is_regressor or mt in ("dt_r", "rf_r", "et_r", "lr_r", "simple_nn_r", "xgb_r", "lgbm_r"):
        if mt == "dt_r":
            return emit_regressor_tree(payload, arrs, prefix, nc, nf, nclass_macro)
        if mt in ("rf_r", "et_r"):
            return emit_regressor_forest(payload, arrs, prefix, nc, nf, nclass_macro)
        if mt == "lr_r":
            return emit_regressor_lr(arrs, prefix, nc, nf, nclass_macro)
        if mt == "simple_nn_r":
            return emit_regressor_mlp(arrs, prefix, nc, nf, nclass_macro)
        if mt in ("xgb_r", "lgbm_r"):
            return emit_gbdt_regressor(payload, arrs, prefix, nc, nf, nclass_macro)

    # ---------- 分类模型 ----------
    if mt == "dt":
        return emit_tree(payload, arrs, prefix, nc, nf, nclass_macro)
    if mt in ("rf", "et"):
        return emit_forest(payload, arrs, prefix, nc, nf, nclass_macro)
    if mt == "lr":
        return emit_lr(arrs, prefix, nc, nf, nclass_macro)
    if mt == "nb":
        return emit_nb(arrs, prefix, nc, nf, nclass_macro)
    if mt == "mlp":
        return emit_mlp(arrs, prefix, nc, nf, nclass_macro)
    if mt == "simple_nn":
        # SimpleNN 二分类：W2_ 为 (2,h)，predict_proba 口径与标准 softmax 不同，
        # 必须走专用发射器；多分类/单输出路径与 MLP 结构相同，复用 emit_mlp。
        if nc == 2 and int(arrs["w2"].shape[0]) == 2:
            return emit_simple_nn_binary(arrs, prefix, nc, nf, nclass_macro)
        return emit_mlp(arrs, prefix, nc, nf, nclass_macro)
    if mt in ("xgb", "lgbm"):
        return emit_gbdt_classifier(payload, arrs, prefix, nc, nf, nclass_macro)
    raise ValueError(f"未知模型类型: {mt}")
