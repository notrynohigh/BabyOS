"""工程 CRUD + 阶段状态机 + 级联失效（FR-1.1~1.4）。"""
from __future__ import annotations

import json
import os
import re
import shutil
import uuid
from pathlib import Path

from ..config import STAGE_INDEX, projects_root
from ..deps import (
    AppError,
    atomic_write_json,
    copytree_clean,
    load_meta,
    project_dir,
    project_exists,
    read_json,
    save_meta,
    utc_now,
)
from ..schemas import ProjectCreate, ProjectMeta, ProjectPatch, ProjectSummary

# 级联失效产物目录（FR-1.4）：键 = 回退目标阶段，值 = 需清除的目录/文件
# 注意：只清"派生产物"（特征矩阵 / 训练 / 导出）。features/config.json 是用户
# 特征工程意图（窗长/步进/特征选择/频域开关），与数据内容无关，必须保留——
# 否则片段编辑等上游变更会静默把特征配置打回默认值（评审 BUG-7）。
_CASCADE = {
    "data_imported": ["labeling", "features/matrix.npz", "features/matrix.meta.json",
                      "training", "export"],
    "labeled": ["features/matrix.npz", "features/matrix.meta.json", "training", "export"],
    "featured": ["training", "export"],
}


def _remove_paths(pdir: Path, names: list[str]) -> None:
    for n in names:
        p = pdir / n
        if p.is_dir():
            shutil.rmtree(p, ignore_errors=True)
        elif p.exists():
            p.unlink()


def _touch_stage(pdir: Path, stage: str) -> None:
    atomic_write_json(pdir / "stage_marker.json", {"stage": stage, "at": utc_now()})


# 工程名禁用：路径分隔符 + Windows 保留字符 + 控制字符（避免后续 zip/路径/section 异常）
# 保留 Unicode（中文/日文/表情）— 仅剥 `/\:*?"<>|` 与 NUL + 收尾空白
_FORBIDDEN_NAME_RE = re.compile(r'[\\/:\*\?"<>\|\x00]')


def _sanitize_name(raw: str) -> str:
    """工程名清洗（评审 A1）：禁路径分隔符与 Windows 保留字符；保留中文等可显示 Unicode。

    三类拒绝：
    1. 纯空白 / 空串 → INVALID_NAME
    2. 全部为禁用字符（剥后全 '_' 或空）→ INVALID_NAME（用户无有效字符意图）
    3. 余下情况：静默替换禁用字符为 '_'，再 strip '_'，最后截断到 64。
    """
    s = _FORBIDDEN_NAME_RE.sub("_", raw).strip()
    if not s:
        raise AppError(422, "INVALID_NAME",
                       f"工程名不能为空或纯空白: {raw!r}")
    # 若 strip 后全是 '_'（即原始名字仅由禁用字符构成）→ 用户意图不清，拒
    s2 = s.strip("_")
    if not s2:
        raise AppError(422, "INVALID_NAME",
                       f"工程名仅含禁用字符（/ \\ : * ? \" < > | NUL），请用有效字符: {raw!r}")
    if len(s2) > 64:
        s2 = s2[:64]
    return s2


class ProjectService:
    def create(self, body: ProjectCreate) -> ProjectMeta:
        if body.mode == "timeseries" and body.task_type == "regression":
            # timeseries 构建矩阵尚不产出 y_float，训练会按 class-id 拟合回归
            # 却导出 N_CLASSES=1 —— 直接拒绝，避免静默错误模型。
            raise AppError(
                422, "UNSUPPORTED_TASK",
                "时序模式暂不支持回归任务（特征矩阵尚未产出连续目标 y_float）",
            )
        pid = str(uuid.uuid4())
        root = projects_root()
        root.mkdir(parents=True, exist_ok=True)
        (root / pid).mkdir()
        clean_name = _sanitize_name(body.name.strip())
        meta = ProjectMeta(
            project_id=pid,
            name=clean_name,
            mode=body.mode,
            task_type=body.task_type,
            sampling_rate=body.sampling_rate,
            created_at=utc_now(),
            updated_at=utc_now(),
        )
        save_meta(meta)
        _touch_stage(project_dir(pid), meta.stage)
        return meta

    def get(self, pid: str) -> ProjectMeta:
        return load_meta(pid)

    def list(self) -> list[ProjectSummary]:
        root = projects_root()
        if not root.is_dir():
            return []
        out = []
        for d in sorted(root.iterdir()):
            if not d.is_dir():
                continue
            try:
                meta = load_meta(d.name)
            except AppError:
                continue
            out.append(self._summary(meta, d))
        return out

    def _summary(self, meta: ProjectMeta, d: Path) -> ProjectSummary:
        n_samples = 0
        matrix = d / "features" / "matrix.npz"
        if matrix.exists():
            try:
                import numpy as np

                with np.load(matrix) as z:
                    n_samples = int(z["X"].shape[0])
            except Exception:
                n_samples = 0
        best_metric = None
        model_type = None
        m = read_json(d / "training" / "best" / "metrics_test.json")
        if m:
            # 根据 task_type 取合适的指标
            task_type = getattr(meta, "task_type", "classification")
            if task_type == "regression":
                best_metric = m.get("r2")
            else:
                best_metric = m.get("f1_macro")
            model_type = m.get("model_type")
        # file_count: from raw/files.json
        files_json = read_json(d / "raw" / "files.json", default={})
        file_count = len(files_json) if isinstance(files_json, dict) else 0
        # segment_count: from labeling/segments.json
        segs = read_json(d / "labeling" / "segments.json", default=[])
        segment_count = len(segs) if isinstance(segs, list) else 0
        # training_status: from training/run_state.json
        run_state = read_json(d / "training" / "run_state.json")
        training_status = (run_state or {}).get("status", "idle")
        return ProjectSummary(
            project_id=meta.project_id,
            name=meta.name,
            mode=meta.mode,
            task_type=getattr(meta, "task_type", "classification"),
            stage=meta.stage,
            export_stale=meta.export_stale,
            labels=meta.labels,
            n_samples=n_samples,
            best_metric=best_metric,
            best_model_type=model_type,
            updated_at=meta.updated_at,
            file_count=file_count,
            segment_count=segment_count,
            training_status=training_status,
        )

    def patch(self, pid: str, body: ProjectPatch) -> ProjectMeta:
        meta = load_meta(pid)
        if body.name is not None:
            meta.name = _sanitize_name(body.name.strip())
        if body.sampling_rate is not None:
            meta.sampling_rate = body.sampling_rate
        save_meta(meta)
        return meta

    def delete(self, pid: str, *, hard: bool = False) -> None:
        """删除工程。评审 P0-FX-5：默认软删除（移到 .trash），可恢复。
        `?hard=true` 走永久删除（保留给管理员/确认对话框二次确认）。
        """
        from ..config import trash_root, trash_retention_days  # 避免循环
        from datetime import datetime, timezone

        d = project_dir(pid)
        if hard:
            shutil.rmtree(d)
            # 也清理 trash 中的同名残留
            tr = trash_root()
            if tr.is_dir():
                for old in tr.glob(f"{pid}_*"):
                    shutil.rmtree(old, ignore_errors=True)
            return

        # 软删除：移到 trash（重命名为 {pid}_{ts} 避免同 pid 二次删除冲突）
        ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        tr = trash_root()
        tr.mkdir(parents=True, exist_ok=True)
        target = tr / f"{pid}_{ts}"
        shutil.move(str(d), str(target))
        # meta.json 已随目录移到 trash，list_trashed() 直接读 trash 内的副本

    def list_trashed(self) -> list[dict]:
        """列出回收站工程。"""
        from ..config import trash_root
        from datetime import datetime

        out = []
        tr = trash_root()
        if not tr.is_dir():
            return out
        for p in sorted(tr.glob("*_*"), reverse=True):  # 新的在前
            if not p.is_dir():
                continue
            # 目录名约定 {pid}_{ts}；ts 不含 '_'，最后一段为 ts
            name = p.name
            # 反向解析 ts（取最后一个 '_' 之后）
            if "_" not in name:
                continue
            pid, ts = name.rsplit("_", 1)
            meta_path = p / "meta.json"
            meta = json.loads(meta_path.read_text()) if meta_path.is_file() else {}
            out.append({
                "trash_id": name,           # 用于 restore 接口
                "project_id": pid,
                "trashed_at": ts,
                "name": meta.get("name"),
                "mode": meta.get("mode"),
            })
        return out

    def restore(self, trash_id: str) -> ProjectMeta:
        """从回收站恢复工程。"""
        from ..config import trash_root
        import json
        from datetime import datetime

        # ── path-traversal guards ──
        if ".." in trash_id or "/" in trash_id or "\\" in trash_id:
            raise AppError(400, "INVALID_TRASH_ID", f"trash_id 包含非法字符: {trash_id!r}")

        tr = trash_root()
        src = (tr / trash_id).resolve()
        if not str(src).startswith(str(tr.resolve()) + os.sep):
            raise AppError(400, "INVALID_TRASH_ID", f"trash_id 越界: {trash_id!r}")

        if not src.is_dir():
            raise AppError(404, "TRASH_NOT_FOUND", f"回收站条目不存在: {trash_id}")
        if "_" not in trash_id:
            raise AppError(422, "INVALID_TRASH_ID", f"trash_id 格式错误: {trash_id!r}")
        pid = trash_id.rsplit("_", 1)[0]
        # 冲突检查：现存工程用同名 pid 不可恢复（极少见，但防御）
        if (projects_root() / pid).is_dir():
            raise AppError(409, "PID_CONFLICT",
                           f"工程 {pid} 已存在，请先删除后再恢复（恢复会覆盖原工程）")
        # 移回 projects_root
        target = (projects_root() / pid).resolve()
        if not str(target).startswith(str(projects_root().resolve()) + os.sep):
            raise AppError(400, "INVALID_TRASH_ID", f"恢复目标越界: {pid!r}")
        shutil.move(str(src), str(target))
        # 读取 meta 返回
        meta_dict = json.loads((target / "meta.json").read_text())
        return ProjectMeta(**meta_dict)

    def sweep_trash(self, *, days: int | None = None) -> int:
        """清理超过保留天数的回收站条目，返回清理条数。"""
        from ..config import trash_root, trash_retention_days
        import time

        tr = trash_root()
        if not tr.is_dir():
            return 0
        threshold = time.time() - (days if days is not None else trash_retention_days()) * 86400
        n = 0
        for p in tr.glob("*_*"):
            if not p.is_dir():
                continue
            try:
                if p.stat().st_mtime < threshold:
                    shutil.rmtree(p, ignore_errors=True)
                    n += 1
            except OSError:
                continue
        return n

    def copy(self, pid: str) -> ProjectMeta:
        src = project_dir(pid)
        meta = load_meta(pid)
        new_id = str(uuid.uuid4())
        dst = projects_root() / new_id
        copytree_clean(src, dst)
        meta.project_id = new_id
        meta.name = meta.name + " 副本"
        meta.created_at = meta.updated_at = utc_now()
        save_meta(meta)
        return meta

    # ---------- 状态机 ----------

    def advance(self, pid: str, stage: str) -> None:
        """推进到不小于当前阶段的任意阶段（幂等）。"""
        meta = load_meta(pid)
        if STAGE_INDEX[stage] > STAGE_INDEX[meta.stage]:
            meta.stage = stage
            save_meta(meta)
            _touch_stage(project_dir(pid), stage)

    def invalidate(self, pid: str, to_stage: str, *, export_stale: bool = True) -> ProjectMeta:
        """级联失效（FR-1.4）：回退到 to_stage，清除下游产物；
        曾处于 exported 的工程在上游修改后标记导出物过期。"""
        meta = load_meta(pid)
        was_exported = meta.stage == "exported"
        if STAGE_INDEX[to_stage] < STAGE_INDEX[meta.stage]:
            _remove_paths(project_dir(pid), _CASCADE.get(to_stage, []))
            meta.stage = to_stage
        if export_stale and (was_exported or meta.export_stale):
            meta.export_stale = True
        save_meta(meta)
        _touch_stage(project_dir(pid), meta.stage)
        return meta

    def mark_exported(self, pid: str) -> None:
        """导出成功：置 exported 且清除过期标记。"""
        meta = load_meta(pid)
        meta.stage = "exported"
        meta.export_stale = False
        save_meta(meta)
        _touch_stage(project_dir(pid), meta.stage)


def require_stage(meta: ProjectMeta, min_stage: str) -> None:
    if STAGE_INDEX[meta.stage] < STAGE_INDEX[min_stage]:
        raise AppError(422, "STAGE_NOT_READY", f"当前阶段 {meta.stage}，需要至少 {min_stage}")


def assert_writable(meta: ProjectMeta, training_active: bool) -> None:
    """训练写锁（design.md §3）：上游修改在活跃训练期间被拒绝。"""
    if training_active:
        raise AppError(409, "TRAINING_ACTIVE", "训练进行中，请先取消或等待完成")


def check_project_id(pid: str) -> None:
    if not project_exists(pid):
        raise AppError(404, "PROJECT_NOT_FOUND", f"工程不存在: {pid}")
