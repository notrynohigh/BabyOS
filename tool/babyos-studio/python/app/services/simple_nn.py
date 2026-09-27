"""SimpleNN — 轻量前馈神经网络，适合 MCU 部署。

单隐藏层 MLP，numpy 实现，sklearn estimator 接口。
导出时可生成 C 推理代码（权重矩阵 + ReLU）。
"""
from __future__ import annotations

import numpy as np
from sklearn.base import BaseEstimator, ClassifierMixin, RegressorMixin
from sklearn.utils.validation import check_is_fitted


class SimpleNNClassifier(BaseEstimator, ClassifierMixin):
    """单隐藏层前馈神经网络（二分类/多分类）。

    Parameters
    ----------
    hidden : int
        隐藏层神经元数（默认 16）。
    learning_rate : float
        学习率（默认 0.01）。
    max_iter : int
        最大迭代次数（默认 500）。
    batch_size : int
        mini-batch 大小（默认 32）。
    alpha : float
        L2 正则化系数（默认 0.001）。
    seed : int
        随机种子。
    """

    def __init__(
        self,
        hidden: int = 16,
        learning_rate: float = 0.01,
        max_iter: int = 500,
        batch_size: int = 32,
        alpha: float = 0.001,
        seed: int = 42,
    ):
        self.hidden = hidden
        self.learning_rate = learning_rate
        self.max_iter = max_iter
        self.batch_size = batch_size
        self.alpha = alpha
        self.seed = seed

    def _init_weights(self, n_in: int, n_out: int, rng: np.random.Generator):
        """He 初始化。"""
        scale = np.sqrt(2.0 / n_in)
        W1 = rng.normal(0, scale, (self.hidden, n_in)).astype(np.float64)
        b1 = np.zeros(self.hidden, dtype=np.float64)
        scale2 = np.sqrt(2.0 / self.hidden)
        W2 = rng.normal(0, scale2, (n_out, self.hidden)).astype(np.float64)
        b2 = np.zeros(n_out, dtype=np.float64)
        return W1, b1, W2, b2

    def _forward(self, X: np.ndarray):
        """前向传播，返回 logits 和中间量（用于反向传播）。"""
        z1 = X @ self.W1_.T + self.b1_          # (n, hidden)
        a1 = np.maximum(0, z1)                    # ReLU
        z2 = a1 @ self.W2_.T + self.b2_          # (n, n_out)
        return z1, a1, z2

    def _softmax(self, logits: np.ndarray) -> np.ndarray:
        e = np.exp(logits - logits.max(axis=1, keepdims=True))
        return e / e.sum(axis=1, keepdims=True)

    def _encode_labels(self, y: np.ndarray):
        classes = np.unique(y)
        self.classes_ = classes
        n_out = len(classes)
        if n_out == 2:
            y_enc = (y == classes[1]).astype(np.float64)
            return y_enc.reshape(-1, 1), n_out
        else:
            y_enc = np.zeros((len(y), n_out), dtype=np.float64)
            for i, c in enumerate(classes):
                y_enc[y == c, i] = 1.0
            return y_enc, n_out

    def fit(self, X: np.ndarray, y: np.ndarray):
        rng = np.random.default_rng(self.seed)
        X = np.asarray(X, dtype=np.float64)
        y = np.asarray(y)
        Y, n_out = self._encode_labels(y)
        n_in = X.shape[1]

        self.W1_, self.b1_, self.W2_, self.b2_ = self._init_weights(n_in, n_out, rng)

        n = X.shape[0]
        for epoch in range(self.max_iter):
            # shuffle
            idx = rng.permutation(n)
            X_sh, Y_sh = X[idx], Y[idx]

            for start in range(0, n, self.batch_size):
                xb = X_sh[start : start + self.batch_size]
                yb = Y_sh[start : start + self.batch_size]
                bs = xb.shape[0]

                # forward
                z1, a1, z2 = self._forward(xb)
                probs = self._softmax(z2)

                # loss gradient (cross-entropy + L2)
                dz2 = (probs - yb) / bs            # (bs, n_out)
                dW2 = dz2.T @ a1 + self.alpha * self.W2_
                db2 = dz2.sum(axis=0)

                da1 = dz2 @ self.W2_
                dz1 = da1 * (z1 > 0).astype(np.float64)  # ReLU grad
                dW1 = dz1.T @ xb + self.alpha * self.W1_
                db1 = dz1.sum(axis=0)

                # update
                self.W2_ -= self.learning_rate * dW2
                self.b2_ -= self.learning_rate * db2
                self.W1_ -= self.learning_rate * dW1
                self.b1_ -= self.learning_rate * db1

        return self

    def predict_proba(self, X: np.ndarray) -> np.ndarray:
        check_is_fitted(self, ["W1_", "b1_", "W2_", "b2_", "classes_"])
        X = np.asarray(X, dtype=np.float64)
        _, _, z2 = self._forward(X)
        probs = self._softmax(z2)
        if len(self.classes_) == 2:
            return np.column_stack([1 - probs[:, 0], probs[:, 0]])
        return probs

    def predict(self, X: np.ndarray) -> np.ndarray:
        proba = self.predict_proba(X)
        indices = proba.argmax(axis=1)
        return self.classes_[indices]

    def get_params(self, deep=True):
        return {
            "hidden": self.hidden,
            "learning_rate": self.learning_rate,
            "max_iter": self.max_iter,
            "batch_size": self.batch_size,
            "alpha": self.alpha,
            "seed": self.seed,
        }

    def set_params(self, **params):
        for k, v in params.items():
            setattr(self, k, v)
        return self


class SimpleNNRegressor(BaseEstimator, RegressorMixin):
    """单隐藏层前馈神经网络（回归）。

    与 SimpleNNClassifier 相同架构，但输出层线性激活 + MSE 损失，
    适合 MCU 上的连续值预测任务。权重结构（W1/b1/W2/b2）与分类版一致，
    C 代码导出可复用 MLP 路径。

    Parameters
    ----------
    hidden : int
        隐藏层神经元数（默认 16）。
    learning_rate : float
        学习率（默认 0.01）。
    max_iter : int
        最大迭代次数（默认 500）。
    batch_size : int
        mini-batch 大小（默认 32）。
    alpha : float
        L2 正则化系数（默认 0.001）。
    seed : int
        随机种子。
    """

    def __init__(
        self,
        hidden: int = 16,
        learning_rate: float = 0.01,
        max_iter: int = 500,
        batch_size: int = 32,
        alpha: float = 0.001,
        seed: int = 42,
    ):
        self.hidden = hidden
        self.learning_rate = learning_rate
        self.max_iter = max_iter
        self.batch_size = batch_size
        self.alpha = alpha
        self.seed = seed

    def _init_weights(self, n_in: int, n_out: int, rng: np.random.Generator):
        """He 初始化。"""
        scale = np.sqrt(2.0 / n_in)
        W1 = rng.normal(0, scale, (self.hidden, n_in)).astype(np.float64)
        b1 = np.zeros(self.hidden, dtype=np.float64)
        scale2 = np.sqrt(2.0 / self.hidden)
        W2 = rng.normal(0, scale2, (n_out, self.hidden)).astype(np.float64)
        b2 = np.zeros(n_out, dtype=np.float64)
        return W1, b1, W2, b2

    def _forward(self, X: np.ndarray):
        """前向传播，返回中间量（用于反向传播）。"""
        z1 = X @ self.W1_.T + self.b1_          # (n, hidden)
        a1 = np.maximum(0, z1)                    # ReLU
        z2 = a1 @ self.W2_.T + self.b2_          # (n, 1) — 线性输出
        return z1, a1, z2

    def fit(self, X: np.ndarray, y: np.ndarray):
        rng = np.random.default_rng(self.seed)
        X = np.asarray(X, dtype=np.float64)
        y = np.asarray(y, dtype=np.float64).reshape(-1, 1)
        n_in = X.shape[1]
        n_out = 1

        self.W1_, self.b1_, self.W2_, self.b2_ = self._init_weights(n_in, n_out, rng)

        n = X.shape[0]
        for epoch in range(self.max_iter):
            idx = rng.permutation(n)
            X_sh, y_sh = X[idx], y[idx]

            for start in range(0, n, self.batch_size):
                xb = X_sh[start : start + self.batch_size]
                yb = y_sh[start : start + self.batch_size]
                bs = xb.shape[0]

                # forward
                z1, a1, z2 = self._forward(xb)

                # MSE loss gradient + L2 regularization
                dz2 = 2.0 * (z2 - yb) / bs       # (bs, 1)
                dW2 = dz2.T @ a1 + self.alpha * self.W2_
                db2 = dz2.sum(axis=0)

                da1 = dz2 @ self.W2_
                dz1 = da1 * (z1 > 0).astype(np.float64)  # ReLU grad
                dW1 = dz1.T @ xb + self.alpha * self.W1_
                db1 = dz1.sum(axis=0)

                # update
                self.W2_ -= self.learning_rate * dW2
                self.b2_ -= self.learning_rate * db2
                self.W1_ -= self.learning_rate * dW1
                self.b1_ -= self.learning_rate * db1

        return self

    def predict(self, X: np.ndarray) -> np.ndarray:
        check_is_fitted(self, ["W1_", "b1_", "W2_", "b2_"])
        X = np.asarray(X, dtype=np.float64)
        _, _, z2 = self._forward(X)
        return z2.ravel()

    def get_params(self, deep=True):
        return {
            "hidden": self.hidden,
            "learning_rate": self.learning_rate,
            "max_iter": self.max_iter,
            "batch_size": self.batch_size,
            "alpha": self.alpha,
            "seed": self.seed,
        }

    def set_params(self, **params):
        for k, v in params.items():
            setattr(self, k, v)
        return self
