"""float32 参考实现（FR-8.5 契约）：与生成 C 同序逐操作对齐。

两条硬规定（design §7.2）：
1. 禁止 np.sum / np.add.reduce —— 一切累加显式循环 acc = np.float32(acc + v)
2. softmax 先减 max 再 exp（np.exp float32）
树判定：(double)x <= thr —— x 先 f32 归一化再转 f64 比较。
"""
from __future__ import annotations

import numpy as np

from .model_cgen import extract_arrays


def _f32(v):
    return np.float32(v)


def _dot_f32(w: np.ndarray, x: np.ndarray) -> np.float32:
    """f32 顺序累加点积：acc = f32(acc + w[i]*x[i])，先乘后加各自舍入。"""
    acc = np.float32(0.0)
    for i in range(x.shape[0]):
        acc = np.float32(acc + np.float32(w[i] * x[i]))
    return acc


def _softmax_f32(z: np.ndarray) -> np.ndarray:
    m = np.max(z)
    e = np.exp(np.float32(z - np.float32(m)), dtype=np.float32)
    s = np.float32(0.0)
    for i in range(e.shape[0]):
        s = np.float32(s + e[i])
    out = np.empty_like(e)
    for i in range(e.shape[0]):
        out[i] = np.float32(e[i] / s)
    return out


def _sigmoid_f32(z: np.float32) -> np.float32:
    """与 bAlgoMlSigmoid 对齐：1 / (1 + expf(-z))（f32）。"""
    return np.float32(1.0) / np.float32(1.0 + np.exp(-z, dtype=np.float32))


def _tree_leaf_f32(tr: dict, xf: np.ndarray) -> np.float32:
    """树遍历返回叶值（f32）。阈值判定 (double)x <= thr，与 C bAlgoMlTreePredict 一致。

    GBDT thr 在 extract_arrays 期已做 XGB nextafter 适配（x < thr ⟺ x <= thr_c）。
    """
    cur = 0
    while True:
        if tr["feat"][cur] < 0:
            return np.float32(tr["values"][cur])
        xv = np.float64(xf[int(tr["feat"][cur])])
        cur = int(tr["left"][cur]) if xv <= tr["thr"][cur] else int(tr["right"][cur])


def _regress_row_f32(arrs: dict, mt: str, xi: np.ndarray) -> np.float32:
    """回归单行 f32 预测：与 emit_regressor_* / emit_gbdt_regressor 逐操作对齐。"""
    trees = arrs.get("trees")
    if mt == "dt_r":
        return _tree_leaf_f32(trees[0], xi)
    if mt in ("rf_r", "et_r"):
        acc = np.float32(0.0)
        n_trees = len(trees)
        for t in range(n_trees):
            acc = np.float32(acc + _tree_leaf_f32(trees[t], xi))
        return np.float32(acc / np.float32(n_trees))
    if mt == "lr_r":
        w = np.asarray(arrs["w"], dtype=np.float32).ravel()
        b = np.float32(float(np.asarray(arrs["b"], dtype=np.float64).ravel()[0]))
        return np.float32(b + _dot_f32(w, xi))
    if mt == "simple_nn_r":
        w1 = np.asarray(arrs["w1"], dtype=np.float32)
        b1 = np.asarray(arrs["b1"], dtype=np.float32)
        w2 = np.asarray(arrs["w2"], dtype=np.float32).ravel()
        b2 = np.float32(float(np.asarray(arrs["b2"], dtype=np.float64).ravel()[0]))
        h = np.empty(w1.shape[0], dtype=np.float32)
        for i in range(w1.shape[0]):
            z = _f32(b1[i])
            z = np.float32(z + _dot_f32(w1[i], xi))
            h[i] = z if z > 0 else np.float32(0.0)
        return np.float32(b2 + _dot_f32(w2, h))
    if mt in ("xgb_r", "lgbm_r"):
        base = np.asarray(arrs.get("base", np.zeros(1)), dtype=np.float64)
        pred = np.float32(float(base.ravel()[0])) if base.size else np.float32(0.0)
        for tr in trees:
            pred = np.float32(pred + _tree_leaf_f32(tr, xi))
        return pred
    raise ValueError(f"未知回归模型类型: {mt}")


def _normalize_f32(x: np.ndarray, scaler: dict) -> np.ndarray:
    """C 侧：xf[j] = (x[j] - offset[j]) * inv_scale[j]，全 f32。

    scaler 快照存 offset/scale（scale 为 std 或 max-min）；C 烘焙 inv_scale=1/scale。
    此处先 f32 除再 f32 乘会与 C 的预烘焙倒数有 1ulp 差 —— 统一改为 f64 求倒数再 f32 乘，
    与生成器烘焙 float(1/scale) 完全一致。
    """
    off = np.asarray(scaler["offset"], dtype=np.float32)
    inv = np.asarray([np.float32(1.0 / np.float64(s)) for s in scaler["scale"]], dtype=np.float32)
    out = np.empty(len(x), dtype=np.float32)
    for j in range(len(x)):
        out[j] = np.float32(np.float32(np.float32(x[j]) - off[j]) * inv[j])
    return out


def _tree_proba_f32(tr: dict, xf: np.ndarray) -> np.ndarray:
    cur = 0
    while True:
        if tr["feat"][cur] < 0:
            return tr["proba"][cur].astype(np.float32)
        xv = np.float64(xf[tr["feat"][cur]])
        cur = tr["left"][cur] if xv <= tr["thr"][cur] else tr["right"][cur]


def _pad_proba(p: np.ndarray, nc: int, class_map: list[int] | None) -> np.ndarray:
    """Pad tree proba (possibly < nc due to missing classes in training fold) to nc."""
    if class_map is None or p.shape[0] == nc:
        return p
    out = np.zeros(nc, dtype=np.float32)
    for target_idx, source_idx in enumerate(class_map):
        if source_idx >= 0 and source_idx < p.shape[0]:
            out[target_idx] = p[source_idx]
    return out


def predict_proba_ref(payload: dict, X: np.ndarray) -> np.ndarray:
    """参考实现 predict_proba。X: (n, nf_exported) float32 原始特征（未归一化，
    且已限导出特征子集——与 C algo_<name>_predict 的 features 入参同语义）。

    回归模型：返回 (n, 1) 标量预测值（C 侧 predict 将预测写入 out[0]，
    返回 argmax(out,1)=0；本参考使 selfcheck 的 id/值比较与 C 对齐）。
    """
    nc = len(payload["labels"])
    arrs = extract_arrays(payload, nc)
    mt = arrs["model_type"]
    out = np.empty((X.shape[0], nc), dtype=np.float32)
    is_regressor = bool(arrs.get("is_regressor", False)) or mt.endswith("_r")

    # ---------- 回归模型 ----------
    if is_regressor:
        for r in range(X.shape[0]):
            xi = _normalize_f32(X[r], payload["scaler"])
            out[r, 0] = _regress_row_f32(arrs, mt, xi)
        return out

    # 当训练 fold 缺少部分类别时，树的 proba 维度 < nc。
    # 用 estimator.classes_ 做映射：缺失类别概率置 0。
    est = payload["estimator"]
    est_classes = getattr(est, "classes_", None)
    if est_classes is not None and len(est_classes) < nc:
        cls_to_idx = {int(c): i for i, c in enumerate(est_classes)}
        class_map = [cls_to_idx.get(k, -1) for k in range(nc)]
    else:
        class_map = None

    for r in range(X.shape[0]):
        xi = _normalize_f32(X[r], payload["scaler"])
        if mt == "dt":
            p = _tree_proba_f32(arrs["trees"][0], xi)
            out[r] = _pad_proba(p, nc, class_map)
        elif mt in ("rf", "et"):
            acc = np.zeros(nc, dtype=np.float32)
            n_trees = len(arrs["trees"])
            for t in range(n_trees):
                p = _tree_proba_f32(arrs["trees"][t], xi)
                pp = _pad_proba(p, nc, class_map)
                for k in range(nc):
                    acc[k] = np.float32(acc[k] + np.float32(pp[k] / np.float32(n_trees)))
            out[r] = acc
        elif mt == "lr":
            if arrs["binary"]:
                z = _f32(arrs["b"][0])
                z = np.float32(z + _dot_f32(arrs["w"][0].astype(np.float32), xi))
                p1 = _sigmoid_f32(z)
                out[r, 1] = p1
                out[r, 0] = np.float32(1.0 - p1)
            else:
                z = np.empty(nc, dtype=np.float32)
                for k in range(nc):
                    zk = _f32(arrs["b"][k])
                    zk = np.float32(zk + _dot_f32(arrs["w"][k].astype(np.float32), xi))
                    z[k] = zk
                out[r] = _softmax_f32(z)
        elif mt == "nb":
            z = np.empty(nc, dtype=np.float32)
            theta = arrs["theta"].astype(np.float32)
            c0 = arrs["nb_c0"].astype(np.float32)
            c1 = arrs["nb_c1"].astype(np.float32)
            for k in range(nc):
                acc = _f32(arrs["prior"][k])
                for j in range(xi.shape[0]):
                    d = np.float32(xi[j] - theta[k][j])
                    acc = np.float32(acc + np.float32(c0[k][j] + np.float32(c1[k][j] * np.float32(d * d))))
                z[k] = acc
            out[r] = _softmax_f32(z)
        elif mt == "mlp":
            w1 = arrs["w1"].astype(np.float32)
            b1 = arrs["b1"].astype(np.float32)
            w2 = arrs["w2"].astype(np.float32)
            b2 = arrs["b2"].astype(np.float32)
            h = np.empty(w1.shape[0], dtype=np.float32)
            for i in range(w1.shape[0]):
                z = _f32(b1[i])
                z = np.float32(z + _dot_f32(w1[i], xi))
                h[i] = z if z > 0 else np.float32(0.0)
            if w2.shape[0] == 1:
                # 二分类 MLP：单输出 + sigmoid（与 LR binary 路径一致）
                z = _f32(b2[0])
                z = np.float32(z + _dot_f32(w2[0], h))
                p1 = _sigmoid_f32(z)
                out[r, 1] = p1
                out[r, 0] = np.float32(1.0 - p1)
            else:
                z2 = np.empty(nc, dtype=np.float32)
                for k in range(nc):
                    z = _f32(b2[k])
                    z = np.float32(z + _dot_f32(w2[k], h))
                    z2[k] = z
                out[r] = _softmax_f32(z2)
        elif mt == "simple_nn":
            w1 = arrs["w1"].astype(np.float32)
            b1 = arrs["b1"].astype(np.float32)
            w2 = arrs["w2"].astype(np.float32)
            b2 = arrs["b2"].astype(np.float32)
            h = np.empty(w1.shape[0], dtype=np.float32)
            for i in range(w1.shape[0]):
                z = _f32(b1[i])
                z = np.float32(z + _dot_f32(w1[i], xi))
                h[i] = z if z > 0 else np.float32(0.0)
            n_out = int(w2.shape[0])
            if n_out == 1:
                z = _f32(b2[0])
                z = np.float32(z + _dot_f32(w2[0], h))
                p1 = _sigmoid_f32(z)
                out[r, 1] = p1
                out[r, 0] = np.float32(1.0 - p1)
            elif nc == 2 and n_out == 2:
                # SimpleNN 二分类：与 estimator.predict_proba / 导出 C 对齐
                # proba[1] = softmax([z0,z1])[0] = sigmoid(z0 - z1)
                z0 = _f32(b2[0])
                z0 = np.float32(z0 + _dot_f32(w2[0], h))
                z1 = _f32(b2[1])
                z1 = np.float32(z1 + _dot_f32(w2[1], h))
                d = np.float32(z0 - z1)
                p1 = _sigmoid_f32(d)
                out[r, 1] = p1
                out[r, 0] = np.float32(1.0 - p1)
            else:
                z2 = np.empty(nc, dtype=np.float32)
                for k in range(nc):
                    z = _f32(b2[k])
                    z = np.float32(z + _dot_f32(w2[k], h))
                    z2[k] = z
                out[r] = _softmax_f32(z2)
        elif mt in ("xgb", "lgbm"):
            trees = arrs["trees"]
            tree_nc = int(arrs.get("tree_nc", 1))
            bases = np.asarray(
                arrs.get("base", np.zeros(max(tree_nc, 1))), dtype=np.float64
            )
            is_binary = bool(arrs.get("is_binary", nc <= 2)) or tree_nc == 1
            if is_binary:
                raw = np.float32(float(bases[0])) if bases.size else np.float32(0.0)
                for t in range(len(trees)):
                    raw = np.float32(raw + _tree_leaf_f32(trees[t], xi))
                p1 = _sigmoid_f32(raw)
                out[r, 1] = p1
                out[r, 0] = np.float32(1.0 - p1)
            else:
                raw = np.zeros(nc, dtype=np.float32)
                for k in range(nc):
                    if k < bases.size:
                        raw[k] = np.float32(float(bases[k]))
                for t in range(len(trees)):
                    val = _tree_leaf_f32(trees[t], xi)
                    k = t % tree_nc
                    if k < nc:
                        raw[k] = np.float32(raw[k] + val)
                out[r] = _softmax_f32(raw)
        else:
            raise ValueError(f"未知模型类型: {mt}")
    return out


def predict_ref(payload: dict, X: np.ndarray) -> np.ndarray:
    """分类：argmax（并列最小索引，与 C algo_ml_argmax 对齐）；回归：返回预测值。"""
    mt = payload.get("model_type", "")
    proba = predict_proba_ref(payload, X)
    if mt.endswith("_r"):
        return proba.reshape(-1).astype(np.float64)
    # np.argmax 首最大值 = 最小索引，与 C 严格 > 扫描一致
    return np.argmax(proba, axis=1).astype(np.int64)
