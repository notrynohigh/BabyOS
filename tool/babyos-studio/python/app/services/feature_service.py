"""特征工程（FR-5）：定长窗口化、特征矩阵、打分、确定性划分。

数值口径说明（与 C 生成器对齐的前提）：
- 时域/频域特征公式必须显式、无歧义，C 生成器逐公式镜像（float32 vs float64 差异
  由导出自检第③步的组合容差 |a-b| ≤ max(1e-3·|b|, 1e-6) 把关）
- std/moment 全部用总体口径（除数 N，非 N-1）
"""
from __future__ import annotations

import hashlib
import json
import math

import numpy as np
from scipy.fft import rfft

from ..deps import AppError, atomic_write_json, project_dir, read_json
from ..schemas import FeatureConfig, ProjectMeta
from .dataset_service import _files_json, load_file_data
from .project_service import ProjectService

_svc = ProjectService()

TIME_FEATURES = ["mean", "std", "variance", "min", "max", "rms", "abs_mean", "ptp", "zcr", "autocorr", "skew", "kurt"]
FREQ_FEATURES = ["spec_centroid", "spec_energy", "dominant_freq", "band_ratio"]
ALL_FEATURES = TIME_FEATURES + FREQ_FEATURES

# 特征分类（对标 Piccolo AI 交互）
FEATURE_CATEGORIES = {
    "statistical": {"label": "📊 统计类", "features": ["mean", "std", "variance", "min", "max", "skew", "kurt"]},
    "amplitude":   {"label": "📈 幅值类", "features": ["rms", "abs_mean", "ptp"]},
    "time_domain": {"label": "〰️ 时域类", "features": ["zcr", "autocorr"]},
    "frequency":   {"label": "🔊 频域类", "features": ["spec_centroid", "spec_energy", "dominant_freq", "band_ratio"]},
}

DEFAULT_CONFIG = FeatureConfig(
    window_len_s=2.0,
    n_per_window=512,
    step=64,  # ≥ n_per_window/10；API 强制约束（旧值 1 在 n_per_window≥10 时会被拒为 STEP_TOO_SMALL）
    feature_ids=[f"{f}" for f in ["mean", "std", "rms", "ptp", "zcr"]],
    freq_enabled=False,
    freq_bands=5,
    norm="zscore",
)

# 默认时域特征列表（特征工程默认勾选）
DEFAULT_FEATURE_IDS = ["mean", "std", "rms", "ptp", "zcr"]


def default_channel_features(channels: list[str]) -> dict[str, list[str]]:
    """为每个通道生成默认特征映射（5 个常用时域特征）。"""
    return {ch: list(DEFAULT_FEATURE_IDS) for ch in channels}


# ---------- 配置 ----------

def get_config(meta: ProjectMeta) -> FeatureConfig:
    raw = read_json(project_dir(meta.project_id) / "features" / "config.json")
    if raw is None:
        if meta.mode == "table":
            # 表格模式：feature_ids 空=使用全部数值列作为特征；窗口参数无意义
            return FeatureConfig(feature_ids=[])
        # 时序模式：返回默认配置，并自动映射 channel_features
        cfg = DEFAULT_CONFIG
        if meta.channels:
            cfg = cfg.model_copy(update={
                "channel_features": default_channel_features(meta.channels),
            })
        return cfg
    return FeatureConfig(**raw)


def put_config(meta: ProjectMeta, cfg: FeatureConfig) -> FeatureConfig:
    """FR-5.6 + FR-1.4：配置变更使 trained 及之后回退 featured。
    table 模式窗口参数无意义：归一为合法默认值，避免 schema gt=0 误拒。"""
    if meta.mode != "timeseries":
        # 表格模式：忽略窗口/频域相关字段，归一为合法值
        cfg = cfg.model_copy(update={
            "window_len_s": max(cfg.window_len_s, 0.001),
            "n_per_window": max(cfg.n_per_window, 16),
            "step": max(cfg.step, 1),
            "freq_bands": max(cfg.freq_bands, 1),
        })
    _validate(meta, cfg)
    atomic_write_json(project_dir(meta.project_id) / "features" / "config.json", cfg.model_dump())
    _svc.invalidate(meta.project_id, "featured")
    return cfg


def _validate(meta: ProjectMeta, cfg: FeatureConfig) -> None:
    if meta.mode != "timeseries":
        # table 模式 feature_ids 空 = 用全部数值列（合法）
        if cfg.feature_ids:
            from .dataset_service import _files_json
            pdir = project_dir(meta.project_id)
            files = _files_json(pdir)
            if files:
                all_cols = set()
                for fm in files.values():
                    all_cols.update(fm.get("cols") or [])
                bad = [f for f in cfg.feature_ids if f not in all_cols]
                if bad:
                    raise AppError(
                        422, "FEATURE_COLUMN_MISSING",
                        f"特征列不存在于已导入数据: {bad}",
                    )
        return
    # 评审 P1-FX-6：timeseries 模式必须 ≥1 个时域特征，避免训练矩阵零维
    if not cfg.feature_ids:
        raise AppError(422, "NO_FEATURES_SELECTED",
                       "时序模式请至少选择 1 个时域特征（如 mean/std/rms）")
    if meta.sampling_rate <= 0:
        # 采样率未设置：允许保存配置（导入 CSV 后可能自动推算），但跳过窗口相关校验
        bad = [f for f in cfg.feature_ids if f not in ALL_FEATURES]
        if bad:
            raise AppError(422, "BAD_FEATURE", f"未知特征: {bad}")
        return
    n = int(math.floor(meta.sampling_rate * cfg.window_len_s + 0.5))
    if n < 2:
        raise AppError(422, "WINDOW_TOO_SMALL", f"窗长过小：N={n} < 2，至少需要 2 个采样点")
    # 使用 effective_n 获取实际计算窗口（含偶数对齐），作为后续校验的基准
    eff_n = effective_n(meta, cfg)
    if cfg.freq_enabled and (eff_n & (eff_n - 1)) != 0:
        raise AppError(422, "FREQ_NEEDS_POW2", f"频域特征要求 N 为 2 的幂，当前 N={eff_n}（FR/设计 P2-2 服务端兜底）")
    bad = [f for f in cfg.feature_ids if f not in ALL_FEATURES]
    if bad:
        raise AppError(422, "BAD_FEATURE", f"未知特征: {bad}")
    freq_sel = [f for f in cfg.feature_ids if f in FREQ_FEATURES]
    if freq_sel and not cfg.freq_enabled:
        raise AppError(422, "FREQ_NOT_ENABLED", f"已选择频域特征 {freq_sel}，但未开启频域开关")
    # 步进校验：步进不得小于窗口点数的 1/10（使用 eff_n 与实际计算一致）
    step = max(cfg.step, 1)
    min_step = max(1, eff_n // 10)
    if step < min_step:
        min_step_sec = round(min_step / meta.sampling_rate, 2) if meta.sampling_rate > 0 else 0
        raise AppError(422, "STEP_TOO_SMALL",
                       f"步进 {step} 不允许，最小为窗口点数的 1/10（即 {min_step} 点 ≈ {min_step_sec}s）")
    # 窗口长度必须与分段长度兼容：至少有 1 个分段能容纳完整窗口
    segs = read_json(project_dir(meta.project_id) / "labeling" / "segments.json", default=[])
    if segs:
        min_seg_len = min(s["end"] - s["start"] for s in segs)
        if min_seg_len < eff_n:
            max_safe_n = max(2, min_seg_len // 2 * 2)  # 确保偶数
            new_window_len_s = round(max_safe_n / meta.sampling_rate, 4)
            if new_window_len_s < 0.001:
                new_window_len_s = 0.001
            raise AppError(
                422, "WINDOW_TOO_LARGE",
                f"窗长 {cfg.window_len_s}s（{eff_n}点）超过最短分段长度（{min_seg_len}点）。"
                f"建议将窗长调整为 {new_window_len_s}s 或更小",
            )
        # 步进超出样本长度：1 个步进就超出最短分段，无法产生有效窗口
        if step > min_seg_len:
            raise AppError(
                422, "STEP_EXCEEDS_SAMPLE",
                f"步进 {step} 超出最短分段长度（{min_seg_len}点），无法产生有效窗口",
            )


def effective_n(meta: ProjectMeta, cfg: FeatureConfig) -> int:
    n = int(math.floor(meta.sampling_rate * cfg.window_len_s + 0.5))
    if n < 2:
        n = 2
    if n % 2 == 1:
        n += 1
    return n


# ---------- 特征公式（C 生成器镜像源） ----------

def _time_features(w: np.ndarray) -> dict:
    """w: (N,) float64，无 NaN（调用方保证）"""
    n = w.shape[0]
    m = float(np.mean(w))
    d = w - m
    m2 = float(np.mean(d * d))
    std = math.sqrt(m2)
    m3 = float(np.mean(d * d * d))
    m4 = float(np.mean(d * d * d * d))
    skew = (m3 / (m2 ** 1.5)) if m2 > 0 else 0.0
    kurt = (m4 / (m2 * m2) - 3.0) if m2 > 0 else 0.0
    zcr = float(np.mean(w[:-1] * w[1:] < 0)) if n > 1 else 0.0
    # 滞后1自相关系数
    if n > 1:
        d_var = m2
        if d_var > 0:
            lag1 = float(np.mean(d[:-1] * d[1:]))
            autocorr = lag1 / d_var
        else:
            autocorr = 0.0
    else:
        autocorr = 0.0
    return {
        "mean": m,
        "std": std,
        "variance": m2,
        "min": float(np.min(w)),
        "max": float(np.max(w)),
        "rms": math.sqrt(float(np.mean(w * w))),
        "abs_mean": float(np.mean(np.abs(w))),
        "ptp": float(np.max(w) - np.min(w)),
        "zcr": zcr,
        "autocorr": autocorr,
        "skew": skew,
        "kurt": kurt,
    }


def _freq_features(w: np.ndarray, fs: float, bands: int) -> dict:
    """w: (N,) float64，N 为 2 的幂。rfft 幅度谱。"""
    n = w.shape[0]
    spec = np.abs(rfft(w))  # bins 0..N/2
    k = np.arange(spec.shape[0])
    freqs = k * fs / n
    total_mag = float(np.sum(spec))
    energy = float(np.sum(spec * spec))
    centroid = float(np.sum(freqs * spec) / total_mag) if total_mag > 0 else 0.0
    dom = float(freqs[int(np.argmax(spec))]) if spec.shape[0] else 0.0
    # 频带能量比：rfft bin 均分为 bands 段，各段能量 / 总能量
    edges = np.linspace(0, spec.shape[0], bands + 1).astype(int)
    out = {}
    for b in range(bands):
        seg = spec[edges[b] : edges[b + 1]]
        out[f"band{b}_ratio"] = float(np.sum(seg * seg) / energy) if energy > 0 else 0.0
    return {"spec_centroid": centroid, "spec_energy": energy, "dominant_freq": dom, **out}


def effective_features(cfg: FeatureConfig) -> list[str]:
    """把组合特征 band_ratio 展开为 band0_ratio..band{k-1}_ratio，与矩阵列序一致。"""
    out = []
    for f in cfg.feature_ids:
        if f == "band_ratio":
            out.extend(f"band{i}_ratio" for i in range(cfg.freq_bands))
        else:
            out.append(f)
    return out


def effective_features_for_channel(ch: str, cfg: FeatureConfig) -> list[str]:
    """获取指定通道的有效特征列表。支持逐通道配置。"""
    if cfg.channel_features and ch in cfg.channel_features:
        ch_feats = cfg.channel_features[ch]
    elif cfg.channel_features is not None:
        # channel_features 存在但未包含此通道 → 该通道无特征
        return []
    else:
        ch_feats = cfg.feature_ids
    out = []
    for f in ch_feats:
        if f == "band_ratio":
            out.extend(f"band{i}_ratio" for i in range(cfg.freq_bands))
        else:
            out.append(f)
    return out


def feature_names(meta: ProjectMeta, cfg: FeatureConfig) -> list[str]:
    if meta.mode == "table":
        # 表格模式：feature_ids 为空时表示用所有列（与 _build_table 对齐）
        return list(cfg.feature_ids) if cfg.feature_ids else list(meta.channels)
    names = []
    for ch in meta.channels:
        for f in effective_features_for_channel(ch, cfg):
            names.append(f"{ch}__{f}")
    return names


# ---------- 矩阵构建 ----------

def _cache_key(meta: ProjectMeta, cfg: FeatureConfig) -> str:
    pdir = project_dir(meta.project_id)
    h = hashlib.sha256()
    h.update(json.dumps(cfg.model_dump(), sort_keys=True).encode())
    segs = read_json(pdir / "labeling" / "segments.json", default=[])
    h.update(json.dumps(segs, sort_keys=True).encode())
    for fid, fm in sorted(_files_json(pdir).items()):
        f = pdir / "raw" / f"{fid}.npz"
        if f.exists():
            st = f.stat()
            h.update(f"{fid}:{st.st_mtime}:{st.st_size}:{fm['rows']}".encode())
    return h.hexdigest()[:16]


def compute(meta: ProjectMeta, cfg: FeatureConfig | None = None) -> dict:
    """构建/复用 matrix.npz，返回维度与丢弃统计（FR-5.1/AC-7）。"""
    if cfg is None:
        cfg = get_config(meta)
    _validate(meta, cfg)
    pdir = project_dir(meta.project_id)
    # 检查是否有分段/数据
    files = _files_json(pdir)
    if not files:
        raise AppError(422, "NO_DATA", "项目无数据文件，请先导入数据")
    if meta.mode == "timeseries":
        segs = read_json(pdir / "labeling" / "segments.json", default=[])
        if not segs:
            raise AppError(422, "NO_SEGMENTS", "项目无分段，请先在标注管理页创建分段")
    (pdir / "features").mkdir(parents=True, exist_ok=True)
    key = _cache_key(meta, cfg)
    cached = read_json(pdir / "features" / "matrix.meta.json")
    if cached and cached.get("key") == key and (pdir / "features" / "matrix.npz").exists():
        return cached

    if meta.mode == "timeseries":
        result = _build_timeseries(meta, cfg, pdir)
    else:
        result = _build_table(meta, cfg, pdir)

    if len(result["y"]) == 0:
        raise AppError(422, "NO_SAMPLES",
                       "特征计算后无有效样本。请检查：分段是否覆盖足够数据、窗长是否过大")
    X = np.asarray(result["X"], dtype=np.float32).reshape(len(result["y"]), -1)
    y = np.asarray(result["y"], dtype=np.int32)
    groups = np.asarray(result["groups"], dtype=np.int32)
    save_kwargs = dict(
        X=X, y=y, groups=groups,
        win_file=np.asarray(result.get("win_file", []), dtype="U64"),
        win_start=np.asarray(result.get("win_start", []), dtype=np.int64),
    )
    # 回归任务：额外保存 float 版本的目标值
    if result.get("y_float") is not None:
        save_kwargs["y_float"] = np.asarray(result["y_float"], dtype=np.float64)
    np.savez_compressed(pdir / "features" / "matrix.npz", **save_kwargs)
    info = {
        "key": key,
        "n_samples": int(X.shape[0]),
        "n_features": int(X.shape[1]),
        "feature_names": feature_names(meta, cfg),
        "dropped_short": result["dropped_short"],
        "dropped_nan": result["dropped_nan"],
        "dropped_conflict": result["dropped_conflict"],
    }
    atomic_write_json(pdir / "features" / "matrix.meta.json", info)
    _svc.advance(meta.project_id, "featured")
    return info


def _build_timeseries(meta: ProjectMeta, cfg: FeatureConfig, pdir):
    n = effective_n(meta, cfg)
    step = max(cfg.step, 1)  # 使用用户配置的步进，最小为 1
    fs = meta.sampling_rate
    segs = read_json(pdir / "labeling" / "segments.json", default=[])
    files = _files_json(pdir)
    by_file: dict[str, list] = {}
    for s in segs:
        if s["label_id"] >= 0:
            by_file.setdefault(s["file_id"], []).append(s)

    data_cache: dict[str, np.ndarray] = {}
    X, y, groups, win_file, win_start = [], [], [], [], []
    dropped_short = dropped_nan = dropped_conflict = 0

    for fid, segs_f in by_file.items():
        if fid not in files:
            continue
        if fid not in data_cache:
            data_cache[fid] = load_file_data(pdir, fid)
        data = data_cache[fid]
        ch_idx = {c: i for i, c in enumerate(files[fid]["cols"])}
        missing = [c for c in meta.channels if c not in ch_idx]
        if missing:
            raise AppError(422, "CHANNEL_MISSING", f"文件 {files[fid]['filename']} 缺少通道列: {missing}")
        for seg in segs_f:
            length = seg["end"] - seg["start"]
            if length < n:
                dropped_short += 1
                continue
            for s0 in range(seg["start"], seg["end"] - n + 1, step):
                w_all = data[s0 : s0 + n, :]
                if np.isnan(w_all).any():
                    dropped_nan += 1
                    continue
                # 防御性冲突检查（片段本就不重叠；窗口在片段内部，正常恒不触发）
                conflict = any(
                    o["id"] != seg["id"] and o["file_id"] == fid and s0 < o["end"] and s0 + n > o["start"]
                    for o in by_file[fid]
                )
                if conflict:
                    dropped_conflict += 1
                    continue
                row = []
                for ch in meta.channels:
                    ch_eff = effective_features_for_channel(ch, cfg)
                    if not ch_eff:
                        continue
                    w = w_all[:, ch_idx[ch]]
                    feats = _time_features(w)
                    if cfg.freq_enabled:
                        feats.update(_freq_features(w, fs, cfg.freq_bands))
                    row.extend(feats[f] for f in ch_eff)
                X.append(row)
                y.append(seg["label_id"])
                groups.append(files[fid]["group_id"])
                win_file.append(fid)
                win_start.append(s0)
    return {
        "X": X,
        "y": y,
        "groups": groups,
        "win_file": win_file,
        "win_start": win_start,
        "dropped_short": dropped_short,
        "dropped_nan": dropped_nan,
        "dropped_conflict": dropped_conflict,
    }


def _build_table(meta: ProjectMeta, cfg: FeatureConfig, pdir):
    files = _files_json(pdir)
    X, y, groups = [], [], []
    y_float = [] if meta.task_type == "regression" else None
    dropped = 0
    for fid, fm in sorted(files.items()):
        ys = fm.get("y")
        if ys is None:
            raise AppError(422, "TABLE_UNLABELED", f"文件 {fm['filename']} 无标签，无法构建样本")
        data = load_file_data(pdir, fid)
        # 表格模式：feature_ids=[] → 全列当特征（feature_names 也走同一逻辑）
        ids = cfg.feature_ids if cfg.feature_ids else list(fm["cols"])
        cols = fm.get("cols") or []
        col_idx = []
        for c in ids:
            if c not in cols:
                raise AppError(
                    422, "FEATURE_COLUMN_MISSING",
                    f"文件 {fm.get('filename') or fid} 缺少特征列 {c!r}，"
                    f"可用列: {cols}",
                )
            col_idx.append(cols.index(c))
        block = data[:, col_idx]
        keep = ~np.isnan(block).any(axis=1)
        dropped += int((~keep).sum())
        X.extend(block[keep].tolist())
        ys_arr = np.asarray(ys)[keep]
        if meta.task_type == "regression":
            yf = fm.get("y_float")
            if yf is not None:
                # 回归：使用导入期保存的原始连续目标值
                yf_arr = np.asarray(yf, dtype=np.float64)[keep]
                y_float.extend(float(v) for v in yf_arr)
                y.extend([0] * int(keep.sum()))  # 占位，回归不使用 int32 y
            else:
                # 兼容旧数据：y 为 label_id，取整作为浮点目标
                y_float.extend(float(v) for v in ys_arr)
                y.extend(int(round(float(v))) for v in ys_arr)
        else:
            y.extend(int(v) for v in ys_arr)
        groups.extend([fm["group_id"]] * int(keep.sum()))
    result = {
        "X": X,
        "y": y,
        "groups": groups,
        "dropped_short": 0,
        "dropped_nan": dropped,
        "dropped_conflict": 0,
    }
    if y_float is not None:
        result["y_float"] = y_float
    return result


def load_matrix(meta: ProjectMeta) -> dict:
    pdir = project_dir(meta.project_id)
    f = pdir / "features" / "matrix.npz"
    if not f.exists():
        raise AppError(422, "NO_MATRIX", "特征矩阵不存在，请先在特征工程页计算特征")
    with np.load(f, allow_pickle=False) as z:
        return {k: z[k] for k in z.files}


# ---------- 确定性划分（design.md R-1） ----------

DEFAULT_SEED = 42


def get_or_create_split(meta: ProjectMeta, seed: int | None = None) -> dict:
    """split.json：scoring/train 首次请求时以默认 seed 预生成；train 的 seed 不同时重生成。"""
    pdir = project_dir(meta.project_id)
    m = load_matrix(meta)
    key = _cache_key(meta, get_config(meta))
    sp = read_json(pdir / "training" / "split.json")
    want_seed = seed if seed is not None else DEFAULT_SEED
    if sp and sp.get("seed") == want_seed and sp.get("matrix_key") == key:
        return sp

    rng = np.random.default_rng(want_seed)
    y = m["y"]
    groups = m["groups"]
    idx = np.arange(len(y))
    if meta.mode == "timeseries":
        ug = np.unique(groups)
        if len(ug) <= 1:
            # 只有1个组：无法做 group split，随机 80/20 划分
            n_test = max(1, int(len(idx) * 0.2))
            perm = rng.permutation(len(idx))
            test_idx = idx[perm[:n_test]]
            train_idx = idx[perm[n_test:]]
            folds = [[train_idx.tolist(), test_idx.tolist()]]
        else:
            n_test = max(1, math.ceil(len(ug) * 0.2))
            test_groups = set(rng.choice(ug, size=n_test, replace=False).tolist())
            test_idx = idx[np.isin(groups, list(test_groups))]
            train_idx = idx[~np.isin(groups, list(test_groups))]
            n_unique_groups = len(np.unique(groups[train_idx]))
            # 检查训练集是否有足够类别（>=2），不足时回退到随机分层划分
            n_train_classes = len(np.unique(y[train_idx]))
            if n_train_classes < 2 or n_unique_groups < 2:
                # 训练集类别不足或组数不足：回退到随机分层划分
                n_classes_total = len(np.unique(y))
                n_samples_per_class = int(np.min(np.bincount(y)))
                from sklearn.model_selection import train_test_split

                # 如果每类样本极少（< 3），不做分层（stratify 会失败），直接随机
                if n_samples_per_class < n_classes_total + 1:
                    # 数据太少，确保每类至少1个留在训练集
                    perm = rng.permutation(len(idx))
                    n_test = min(int(len(idx) * 0.2), max(0, len(idx) - n_classes_total))
                    n_test = max(1, n_test)
                    test_idx = perm[:n_test]
                    train_idx = perm[n_test:]
                else:
                    train_idx, test_idx = train_test_split(
                        idx, test_size=0.2, random_state=want_seed, stratify=y
                    )
                # 检查训练集是否足够做交叉验证
                min_class_count = int(np.min(np.bincount(y[train_idx]))) if len(train_idx) > 0 else 0
                if min_class_count >= 2 and len(train_idx) >= 4:
                    from sklearn.model_selection import StratifiedKFold
                    k_eff = min(5, min_class_count)
                    skf = StratifiedKFold(n_splits=max(2, k_eff), shuffle=True, random_state=want_seed)
                    folds = [
                        [train_idx[a].tolist(), train_idx[b].tolist()]
                        for a, b in skf.split(np.zeros(len(train_idx)), y[train_idx])
                    ]
                else:
                    # 样本极少，仅用 train/test 划分，不做交叉验证
                    folds = [[train_idx.tolist(), test_idx.tolist()]]
            else:
                from sklearn.model_selection import GroupKFold
                k_eff = min(5, n_unique_groups)
                gkf = GroupKFold(n_splits=k_eff)
                folds = [
                    [train_idx[a].tolist(), train_idx[b].tolist()]
                    for a, b in gkf.split(np.zeros(len(train_idx)), groups=groups[train_idx])
                ]
    else:
        if meta.task_type == "regression":
            # 回归：随机划分 + KFold（不能用 StratifiedKFold，连续目标无意义）
            from sklearn.model_selection import KFold, train_test_split

            train_idx, test_idx = train_test_split(idx, test_size=0.2, random_state=want_seed)
            k_eff = min(5, len(train_idx))
            kf = KFold(n_splits=max(2, k_eff), shuffle=True, random_state=want_seed)
            folds = [[train_idx[a].tolist(), train_idx[b].tolist()] for a, b in kf.split(np.zeros(len(train_idx)))]
        else:
            from sklearn.model_selection import StratifiedKFold, train_test_split

            train_idx, test_idx = train_test_split(idx, test_size=0.2, random_state=want_seed, stratify=y)
            k_eff = min(5, int(np.min(np.bincount(y[train_idx]))))
            skf = StratifiedKFold(n_splits=max(2, k_eff), shuffle=True, random_state=want_seed)
            folds = [[train_idx[a].tolist(), train_idx[b].tolist()] for a, b in skf.split(np.zeros(len(train_idx)), y[train_idx])]

    sp = {
        "seed": want_seed,
        "matrix_key": key,
        "mode": meta.mode,
        "train_idx": train_idx.tolist(),
        "test_idx": test_idx.tolist(),
        "folds": folds,
    }
    atomic_write_json(pdir / "training" / "split.json", sp)
    return sp


# ---------- 打分（FR-5.4：仅用 80% 训练部分拟合） ----------

def scoring(meta: ProjectMeta, method: str = "f_test", task_type: str = "classification") -> list[dict]:
    m = load_matrix(meta)
    sp = get_or_create_split(meta)
    cfg = get_config(meta)
    names = feature_names(meta, cfg)
    train_idx = np.asarray(sp["train_idx"])
    if len(train_idx) < 2:
        raise AppError(422, "INSUFFICIENT_DATA",
                       f"训练样本不足（{len(train_idx)}条），无法评分。请增加数据量或减小窗长。")
    Xtr = m["X"][train_idx].astype(np.float64)
    if task_type == "regression":
        # 回归：使用浮点目标值（y_float），而非占位 y
        ytr = m["y_float"][train_idx] if "y_float" in m else m["y"][train_idx].astype(np.float64)
    else:
        ytr = m["y"][train_idx]

    # 回归任务使用回归专用特征选择方法
    if task_type == "regression":
        scores = np.zeros(Xtr.shape[1])
        if method == "variance":
            scores = np.var(Xtr, axis=0)
        elif method == "f_test":
            from sklearn.feature_selection import f_regression
            scores, _ = f_regression(Xtr, ytr)
        else:
            from sklearn.feature_selection import mutual_info_regression
            scores = mutual_info_regression(Xtr, ytr, random_state=DEFAULT_SEED)
        scores = np.nan_to_num(scores, nan=0.0)
        order = np.argsort(-scores)
        return [{"feature": names[i], "index": int(i), "score": float(scores[i])} for i in order]

    # 分类任务
    n_classes = len(np.unique(ytr))
    if n_classes < 2:
        # 数据极少时（如仅有2个组且分组后训练集只有1类），用 train_idx 子集降级评分
        X_train_sub = m["X"][train_idx].astype(np.float64)
        y_train_sub = m["y"][train_idx]
        if method == "variance":
            scores = np.var(X_train_sub, axis=0)
            order = np.argsort(-scores)
            return [{"feature": names[i], "index": int(i), "score": float(scores[i])} for i in order]
        # 对于需要类别标签的方法（f_test/mutual_info），用 train_idx 子集降级评分
        if method == "f_test":
            from sklearn.feature_selection import f_classif
            scores, _ = f_classif(X_train_sub, y_train_sub)
        else:
            from sklearn.feature_selection import mutual_info_classif
            scores = mutual_info_classif(X_train_sub, y_train_sub, random_state=DEFAULT_SEED)
        scores = np.nan_to_num(scores, nan=0.0)
        order = np.argsort(-scores)
        return [{"feature": names[i], "index": int(i), "score": float(scores[i])} for i in order]
    scores = np.zeros(Xtr.shape[1])
    if method == "variance":
        scores = np.var(Xtr, axis=0)
    else:
        from sklearn.feature_selection import f_classif, mutual_info_classif

        if method == "f_test":
            scores, _ = f_classif(Xtr, ytr)
        else:
            scores = mutual_info_classif(Xtr, ytr, random_state=DEFAULT_SEED)
        scores = np.nan_to_num(scores, nan=0.0)
    order = np.argsort(-scores)
    return [{"feature": names[i], "index": int(i), "score": float(scores[i])} for i in order]
