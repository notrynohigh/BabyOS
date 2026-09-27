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
    且已限导出特征子集——与 C algo_<name>_predict 的 features 入参同语义）。"""
    nc = len(payload["labels"])
    arrs = extract_arrays(payload, nc)
    mt = arrs["model_type"]
    out = np.empty((X.shape[0], nc), dtype=np.float32)

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
                p1 = np.float32(1.0) / np.float32(1.0 + np.exp(-z, dtype=np.float32))
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
                p1 = np.float32(1.0) / np.float32(1.0 + np.exp(-z, dtype=np.float32))
                out[r, 1] = p1
                out[r, 0] = np.float32(1.0 - p1)
            else:
                z2 = np.empty(nc, dtype=np.float32)
                for k in range(nc):
                    z = _f32(b2[k])
                    z = np.float32(z + _dot_f32(w2[k], h))
                    z2[k] = z
                out[r] = _softmax_f32(z2)
        else:
            raise ValueError(f"未知模型类型: {mt}")
    return out


def predict_ref(payload: dict, X: np.ndarray) -> np.ndarray:
    """argmax（并列最小索引，与 C algo_ml_argmax 对齐）。"""
    proba = predict_proba_ref(payload, X)
    # np.argmax 首最大值 = 最小索引，与 C 严格 > 扫描一致
    return np.argmax(proba, axis=1).astype(np.int64)
