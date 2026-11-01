"""BabyOS AutoML 平台后端配置。"""
import os
from pathlib import Path

# 仓库内默认数据根（design.md §3：AUTOML_DATA_ROOT 可覆盖，测试注入 tmp 目录）
_DEFAULT_ROOT = Path(__file__).resolve().parents[2] / "data"


def data_root() -> Path:
    return Path(os.environ.get("AUTOML_DATA_ROOT", str(_DEFAULT_ROOT)))


def projects_root() -> Path:
    return data_root() / "projects"


def trash_root() -> Path:
    """软删除回收站（评审 P0-FX-5）：误删可恢复，30 天 sweep。

    目录布局：data/.trash/{pid}_{ts}/ — ts 是 ISO 格式时间戳，便于人读
    + 按字典序排序时新→旧自然有序。
    """
    return data_root() / ".trash"


def trash_retention_days() -> int:
    """回收站保留天数；超过自动 sweep。"""
    return int(os.environ.get("AUTOML_TRASH_DAYS", "30"))


def host() -> str:
    return os.environ.get("AUTOML_HOST", "127.0.0.1")


def port() -> int:
    return int(os.environ.get("AUTOML_PORT", "8000"))


# 上传限制（FR-2.1）：单文件 200MB
MAX_FILE_BYTES = 200 * 1024 * 1024

# 阶段状态机（FR-1.2），顺序即推进顺序
STAGES = ["created", "data_imported", "labeled", "featured", "trained", "exported"]
STAGE_INDEX = {s: i for i, s in enumerate(STAGES)}
