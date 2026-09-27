"""数据导入/查询（FR-2）：CSV 流式校验 → npz，group 管理，RLE 标签切分。"""
from __future__ import annotations

import io
import shutil
import uuid
from pathlib import Path

import numpy as np
import pandas as pd
from fastapi import UploadFile

from ..config import STAGE_INDEX
from ..deps import AppError, atomic_write_json, project_dir, read_json, save_meta
from ..schemas import ProjectMeta
from .project_service import ProjectService

CHUNK = 100_000

# 每文件导入结果
# ok: {file_id, filename, group_id, rows, cols, dropped_nan_rows, segments_added}
# err: {filename, code, detail}
ImportResult = dict

_svc = ProjectService()


def _files_json(pdir: Path) -> dict:
    return read_json(pdir / "raw" / "files.json", default={})


def _save_files_json(pdir: Path, files: dict) -> None:
    atomic_write_json(pdir / "raw" / "files.json", files)


def _next_group_id(files: dict) -> int:
    gs = [f.get("group_id", 0) for f in files.values()]
    return (max(gs) + 1) if gs else 0


def _next_label_id(meta: ProjectMeta) -> int:
    ids = [l.label_id for l in meta.labels]
    return (max(ids) + 1) if ids else 0


def _find_or_add_label(meta: ProjectMeta, name: str) -> int:
    for l in meta.labels:
        if l.name == name:
            return l.label_id
    nid = _next_label_id(meta)
    palette = ["#2f80ed", "#27ae60", "#e6a23c", "#eb5757", "#9b51e0", "#00b8d4", "#f2994a", "#6fcf97"]
    from ..schemas import LabelDef

    meta.labels.append(LabelDef(label_id=nid, name=name, color=palette[nid % len(palette)]))
    return nid


def _validate_columns(df: pd.DataFrame, col_names: list[str], kind: str) -> None:
    missing = [c for c in col_names if c not in df.columns]
    if missing:
        raise AppError(422, "COLUMN_MISSING", f"CSV 缺少列: {missing}")
    if kind == "channels" and len(col_names) < 1:
        raise AppError(422, "NO_CHANNELS", "至少选择 1 个通道列")


def _read_csv_safe(content: bytes, filename: str) -> pd.DataFrame:
    if not content:
        raise AppError(422, "EMPTY_FILE", f"{filename}: 空文件")
    try:
        df = pd.read_csv(io.BytesIO(content))
    except pd.errors.ParserError as e:
        raise AppError(422, "BAD_CSV", f"{filename}: 解析失败 {e}")
    except Exception as e:
        raise AppError(422, "BAD_CSV", f"{filename}: {e}")
    if df.empty:
        raise AppError(422, "EMPTY_FILE", f"{filename}: 无数据行")
    return df


def _to_numeric(df: pd.DataFrame, cols: list[str], filename: str) -> pd.DataFrame:
    out = df.copy()
    for c in cols:
        out[c] = pd.to_numeric(out[c], errors="coerce")
        if out[c].notna().sum() == 0:
            raise AppError(422, "NON_NUMERIC_COL", f"{filename}: 列 {c} 全为非数值")
    return out


def preview_csv(content: bytes, filename: str) -> dict:
    """前端列配置前的预览：列名 + 前 20 行 + 类型推断（FR-2.2）"""
    df = _read_csv_safe(content[: 8 * 1024 * 1024], filename)  # 预览截断
    head = df.head(20)
    inferred = {}
    for c in df.columns:
        s = pd.to_numeric(df[c], errors="coerce")
        inferred[c] = "numeric" if s.notna().mean() > 0.9 else "string"
    return {
        "filename": filename,
        "columns": list(df.columns),
        "inferred": inferred,
        "rows": head.where(pd.notna(head), None).to_dict(orient="records"),
    }


def import_files(
    meta: ProjectMeta,
    files: list[UploadFile],
    mapping: dict,
    import_kind: str,
) -> dict:
    """mapping: {channels: [...], ts_col: str|None, label_col: str|None}
    （表格模式 channels=特征列, label_col 必填；前端表格模式用 key "features"，在此归一）。
    逐文件独立校验（FR-2.1 部分失败语义）。"""
    if meta.mode == "table":
        mapping = {
            **mapping,
            "channels": mapping.get("features") or mapping.get("channels") or [],
        }
    if not mapping.get("channels"):
        raise AppError(422, "NO_FEATURES", "未选择任何特征/通道列")
    pdir = project_dir(meta.project_id)
    results = []

    if import_kind == "replace":
        shutil.rmtree(pdir / "raw", ignore_errors=True)
        shutil.rmtree(pdir / "labeling", ignore_errors=True)
        meta.labels = []  # 标签体系随数据重来
    (pdir / "raw").mkdir(parents=True, exist_ok=True)
    files_meta = _files_json(pdir)

    for uf in files:
        try:
            content = uf.file.read()
            if len(content) > 200 * 1024 * 1024:
                # 评审 A2：给可操作建议。MCU 场景原始波形可先降采样（如每 10 点取 1）
                # 或按时间窗分批导入；前端看到此 code 应提示用户。
                size_mb = round(len(content) / (1024 * 1024), 1)
                raise AppError(
                    422, "FILE_TOO_LARGE",
                    f"{uf.filename}: {size_mb}MB，超过 200MB 上限。"
                    f"建议先采样降量（如每 N 点取 1）或按时间窗分批导入",
                )
            res = _import_one(meta, pdir, files_meta, content, uf.filename, mapping)
            results.append({"filename": uf.filename, "ok": True, **res})
        except AppError as e:
            results.append(
                {
                    "filename": uf.filename,
                    "ok": False,
                    "code": e.code,
                    "detail": str(e.detail.get("detail") if isinstance(e.detail, dict) else e.detail),
                }
            )

    _save_files_json(pdir, files_meta)
    ok_any = any(r["ok"] for r in results)
    if ok_any:
        save_meta(meta)
        if import_kind == "replace":
            # replace 导入已清空 raw/labeling 并重建了 segments，
            # 只清理 features/training/export 残留，不动 labeling/（刚创建的）
            for d in ("features", "training", "export"):
                shutil.rmtree(pdir / d, ignore_errors=True)
            # 回退 stage 到 created，让 advance 推进到正确阶段
            meta.stage = "created"
            save_meta(meta)
        else:
            # 追加导入：旧标注保留，trained 及之后回退 featured（FR-1.4）
            _svc.invalidate(meta.project_id, "featured")
        _svc.advance(meta.project_id, "data_imported")
        # 表格模式无 segments，导入时若带 label_col 即视作已"labeled"
        # （AC-16 极简路径：无需 segments service 介入）
        if meta.mode == "table" and any(r.get("label_col_present") for r in results):
            _svc.advance(meta.project_id, "labeled")
        # 时序模式：CSV 自动切片产生 segments 后也应进入 labeled 阶段
        if meta.mode == "timeseries" and any(r.get("segments_added", 0) > 0 for r in results):
            _svc.advance(meta.project_id, "labeled")
    return {"results": results, "imported": sum(1 for r in results if r["ok"])}


def _import_one(
    meta: ProjectMeta,
    pdir: Path,
    files_meta: dict,
    content: bytes,
    filename: str,
    mapping: dict,
) -> dict:
    channels: list[str] = mapping.get("channels") or []
    ts_col = mapping.get("ts_col")
    label_col = mapping.get("label_col")

    df = _read_csv_safe(content, filename)
    need = list(channels) + ([ts_col] if ts_col else []) + ([label_col] if label_col else [])
    _validate_columns(df, need, "channels")
    df = _to_numeric(df, channels + ([ts_col] if ts_col else []), filename)

    fid = uuid.uuid4().hex[:12]
    group_id = _next_group_id(files_meta)
    dropped_nan_rows = 0

    if meta.mode == "table":
        # FR-2.7：含缺失行丢弃并计数
        core = df[channels + ([label_col] if label_col else [])]
        keep = core.notna().all(axis=1)
        dropped_nan_rows = int((~keep).sum())
        df = df[keep].reset_index(drop=True)
        if df.empty:
            raise AppError(422, "ALL_ROWS_DROPPED", f"{filename}: 缺失值丢弃后无剩余行")

    data = df[channels].to_numpy(dtype=np.float64)
    np.savez_compressed(pdir / "raw" / f"{fid}.npz", data=data)
    files_meta[fid] = {
        "filename": filename,
        "group_id": group_id,
        "rows": int(data.shape[0]),
        "cols": channels,
        "dropped_nan_rows": dropped_nan_rows,
        "label_col": label_col,
    }

    segments_added = 0
    if label_col and meta.mode == "timeseries":
        segments_added = _rle_segments(meta, pdir, fid, df[label_col])
    elif label_col and meta.mode == "table":
        for v in df[label_col].dropna().unique():
            _find_or_add_label(meta, str(v))
        files_meta[fid]["y"] = [_find_or_add_label(meta, str(v)) for v in df[label_col]]
        # 表格标签行 → segments.json 中以整文件行区间表达，供统一查询（start/end 为行号）
        segs = read_json(pdir / "labeling" / "segments.json", default=[])
        segs.append(
            {
                "id": uuid.uuid4().hex[:12],
                "file_id": fid,
                "start": 0,
                "end": int(len(df)),
                "label_id": -1,  # 表格模式：真实标签在 files_meta[fid]['y'] 逐行
                "source": "table",
            }
        )
        atomic_write_json(pdir / "labeling" / "segments.json", segs)

    # 更新 meta.channels / sampling_rate（首次导入时固化）
    if not meta.channels:
        meta.channels = channels
    # 时序模式：从 timestamp 列自动推算采样率（Hz）
    if meta.mode == "timeseries" and ts_col and meta.sampling_rate <= 0 and len(df) >= 2:
        ts = df[ts_col].to_numpy(dtype=np.float64)
        dt = np.diff(ts)
        dt = dt[dt > 0]  # 过滤零/负间隔
        if len(dt) > 0:
            median_dt = float(np.median(dt))
            if median_dt > 0:
                meta.sampling_rate = round(1.0 / median_dt, 6)
    return {
        "file_id": fid,
        "group_id": group_id,
        "rows": int(data.shape[0]),
        "segments_added": segments_added,
        "label_col_present": bool(label_col and meta.mode == "table"),
    }


def _rle_segments(meta: ProjectMeta, pdir: Path, fid: str, label_series: pd.Series) -> int:
    """FR-2.6：标签列按值变化点切分片段（run-length）。"""
    segs = read_json(pdir / "labeling" / "segments.json", default=[])
    arr = label_series.to_numpy()
    n = len(arr)
    i = 0
    added = 0
    while i < n:
        if pd.isna(arr[i]):
            i += 1
            continue
        j = i
        while j + 1 < n and (pd.isna(arr[j + 1]) or arr[j + 1] == arr[i]):
            j += 1
        # 段 [i, j]（含 NaN 空洞并入该段，特征阶段按 NaN 丢弃窗口处理）
        lid = _find_or_add_label(meta, str(arr[i]))
        segs.append(
            {"id": uuid.uuid4().hex[:12], "file_id": fid, "start": int(i), "end": int(j) + 1, "label_id": lid, "source": "rle"}
        )
        added += 1
        i = j + 1
    atomic_write_json(pdir / "labeling" / "segments.json", segs)
    return added


def dataset_info(meta: ProjectMeta) -> dict:
    """数据展示（FR-4.2/4.3 的原始层统计）。字段与前端 Explore 页对齐。"""
    pdir = project_dir(meta.project_id)
    files = _files_json(pdir)
    segs = read_json(pdir / "labeling" / "segments.json", default=[])
    per_class: dict[str, int] = {}
    by_channel: dict[str, list] = {}
    total_rows = 0
    for fid, fm in files.items():
        f = pdir / "raw" / f"{fid}.npz"
        if not f.exists():
            continue
        with np.load(f) as z:
            data = z["data"]
        total_rows += int(data.shape[0])
        for ci, ch in enumerate(fm["cols"]):
            by_channel.setdefault(ch, []).append(data[:, ci])
    # 跨文件聚合（同名列拼接；长度不一也能算）
    stats = []
    for ch, cols in by_channel.items():
        cat = np.concatenate(cols)
        stats.append(
            {
                "name": ch,
                "min": float(np.nanmin(cat)),
                "max": float(np.nanmax(cat)),
                "mean": float(np.nanmean(cat)),
                "std": float(np.nanstd(cat)),
                "missing": int(np.isnan(cat).sum()),
            }
        )
    if meta.mode == "timeseries":
        for s in segs:
            lbl = next((l.name for l in meta.labels if l.label_id == s["label_id"]), None)
            if lbl:
                per_class[lbl] = per_class.get(lbl, 0) + 1
    else:
        for fid, fm in files.items():
            for y in fm.get("y", []):
                lbl = next((l.name for l in meta.labels if l.label_id == y), None)
                if lbl:
                    per_class[lbl] = per_class.get(lbl, 0) + 1
    return {
        "files": [{"file_id": k, **{kk: vv for kk, vv in v.items() if kk != "y"}} for k, v in files.items()],
        "columns": meta.channels,
        "groups": sorted({v["group_id"] for v in files.values()}),
        "total_rows": total_rows,
        "n_segments": len(segs),
        "per_class": per_class,
        "class_counts": [{"name": k, "count": v} for k, v in per_class.items()],
        "channels": stats,
        "channel_stats": stats,
    }


def file_data(meta: ProjectMeta, fid: str, limit: int = 200, offset: int = 0) -> dict:
    """单文件原始数据（前端曲线/框选用），支持 offset/limit 分页预览。"""
    import numpy as np

    pdir = project_dir(meta.project_id)
    fm = _files_json(pdir).get(fid)
    if fm is None:
        raise AppError(404, "FILE_NOT_FOUND", f"文件不存在: {fid}")
    data = load_file_data(pdir, fid)
    rows, cols = data.shape
    # Clamp offset
    offset = max(0, min(offset, rows))
    end = min(offset + limit, rows)
    sliced = data[offset:end]
    return {
        "file_id": fid,
        "filename": fm["filename"],
        "columns": fm["cols"],
        "rows": int(rows),
        "offset": int(offset),
        "limit": int(limit),
        "data": np.nan_to_num(sliced, nan=0.0).tolist(),
    }


def load_file_data(pdir: Path, fid: str) -> np.ndarray:
    with np.load(pdir / "raw" / f"{fid}.npz") as z:
        return z["data"]


def delete_file(meta: ProjectMeta, fid: str) -> dict:
    """删除指定数据文件及其关联分段。"""
    pdir = project_dir(meta.project_id)
    files = _files_json(pdir)
    if fid not in files:
        raise AppError(404, "FILE_NOT_FOUND", f"文件不存在: {fid}")

    # 删除 npz 文件
    npz_path = pdir / "raw" / f"{fid}.npz"
    if npz_path.exists():
        npz_path.unlink()

    # 从 files.json 中移除
    del files[fid]
    _save_files_json(pdir, files)

    # 删除关联分段
    segs = read_json(pdir / "labeling" / "segments.json", default=[])
    remaining = [s for s in segs if s.get("file_id") != fid]
    if len(remaining) < len(segs):
        atomic_write_json(pdir / "labeling" / "segments.json", remaining)

    # 级联失效
    _svc.invalidate(meta.project_id, "data_imported")
    return {"ok": True, "deleted": fid}
