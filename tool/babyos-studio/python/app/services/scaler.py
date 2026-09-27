"""归一化 scaler（FR-5.3）：与 C 导出格式一一对应。

C 端推理公式统一为 x' = (x - offset) / scale，三种模式：
- zscore : offset=均值, scale=总体标准差（std==0 时置 1.0 防护，FR-8 零尺度防护）
- minmax : offset=min,  scale=max-min（==0 时置 1.0）
- none   : offset=0,    scale=1
"""
from __future__ import annotations

import numpy as np


class Scaler:
    def __init__(self, mode: str = "zscore") -> None:
        assert mode in ("zscore", "minmax", "none")
        self.mode = mode
        self.offset: np.ndarray | None = None
        self.scale: np.ndarray | None = None
        self.guarded: list[bool] = []

    def fit(self, X: np.ndarray) -> "Scaler":
        if self.mode == "none":
            self.offset = np.zeros(X.shape[1], dtype=np.float64)
            self.scale = np.ones(X.shape[1], dtype=np.float64)
        elif self.mode == "zscore":
            self.offset = X.mean(axis=0)
            self.scale = X.std(axis=0)  # ddof=0 总体口径
        else:
            self.offset = X.min(axis=0)
            self.scale = X.max(axis=0) - self.offset
        self.guarded = [bool(s == 0.0) for s in self.scale]
        self.scale = np.where(self.scale == 0.0, 1.0, self.scale)
        return self

    def transform(self, X: np.ndarray) -> np.ndarray:
        return (X - self.offset) / self.scale

    def snapshot(self) -> dict:
        return {
            "mode": self.mode,
            "offset": [float(v) for v in self.offset],
            "scale": [float(v) for v in self.scale],
            "guarded": list(self.guarded),
        }

    @classmethod
    def from_snapshot(cls, snap: dict) -> "Scaler":
        s = cls(snap["mode"])
        s.offset = np.asarray(snap["offset"], dtype=np.float64)
        s.scale = np.asarray(snap["scale"], dtype=np.float64)
        s.guarded = list(snap.get("guarded", []))
        return s
