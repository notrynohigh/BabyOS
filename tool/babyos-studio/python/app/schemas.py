"""pydantic schema 定义（design.md §4.2/§5）。Python 3.8: 注解惰性化以支持 list[] 泛型。"""
from __future__ import annotations

from typing import Literal, Optional

from pydantic import BaseModel, Field, field_validator


class LabelDef(BaseModel):
    label_id: int = Field(ge=0)
    name: str = Field(min_length=1, max_length=64)
    color: str = "#2f80ed"

    @field_validator("name")
    @classmethod
    def _name_not_blank(cls, v: str) -> str:
        if not v.strip():
            raise ValueError("标签名不能为空或纯空白")
        return v.strip()


class ProjectMeta(BaseModel):
    project_id: str
    name: str
    mode: Literal["timeseries", "table"] = "timeseries"
    task_type: Literal["classification", "regression"] = "classification"
    stage: str = "created"
    export_stale: bool = False
    sampling_rate: float = 0.0
    channels: list[str] = Field(default_factory=list)
    labels: list[LabelDef] = Field(default_factory=list)
    created_at: str = ""
    updated_at: str = ""


class ProjectCreate(BaseModel):
    name: str = Field(min_length=1, max_length=64)
    mode: Literal["timeseries", "table"] = "timeseries"
    task_type: Literal["classification", "regression"] = "classification"
    sampling_rate: float = Field(default=0.0, gt=0)


class ProjectPatch(BaseModel):
    name: Optional[str] = Field(default=None, min_length=1, max_length=64)
    sampling_rate: Optional[float] = Field(default=None, gt=0)


class ProjectSummary(BaseModel):
    """工程列表项（FR-1.3：阶段、样本数、最佳模型 Test 指标）"""

    project_id: str
    name: str
    mode: str
    task_type: str = "classification"
    stage: str
    export_stale: bool
    labels: list[LabelDef]
    n_samples: int = 0
    best_metric: Optional[float] = None
    best_model_type: Optional[str] = None
    updated_at: str = ""
    file_count: int = 0
    segment_count: int = 0
    training_status: str = "idle"


class SegmentIn(BaseModel):
    """片段输入（FR-3.3）：start/end/label_id 均需 ≥ 0 且 end > start。"""
    file_id: str = Field(min_length=1)
    start: int = Field(ge=0)
    end: int = Field(ge=0)
    label_id: int = Field(ge=0)

    @field_validator("end")
    @classmethod
    def _end_gt_start(cls, v: int, info) -> int:
        # pydantic v2: 通过 info.data 访问前面字段
        start = info.data.get("start")
        if start is not None and v <= start:
            raise ValueError(f"片段 end ({v}) 必须 > start ({start})")
        return v


class Segment(SegmentIn):
    id: str
    source: Literal["manual", "rle"] = "manual"


class FeatureConfig(BaseModel):
    """特征配置（FR-5）。
    table 模式 feature_ids 可空（语义：使用全部数值列作为特征）；
    timeseries 模式 feature_ids 必填 ≥1（评审 P1-FX-6：防映射空缺导致训练零维）。
    service 层 put_config 会按 mode 加固约束（schema 不带 mode 上下文，
    故不在此处对 window/step 加 gt/ge 强约束）。

    channel_features: 逐通道特征选择映射。
    - None 或不存在 = 向后兼容，所有通道用 feature_ids 中的全部特征
    - {"accel_x": ["mean","std"], "gyro_x": ["zcr"]} = 逐通道指定"""
    window_len_s: float = 2.0
    n_per_window: int = 512
    step: int = Field(default=1, ge=1)
    feature_ids: list[str] = Field(default_factory=list)
    channel_features: dict[str, list[str]] | None = None
    freq_enabled: bool = False
    freq_bands: int = 5
    norm: Literal["none", "minmax", "zscore", "robust"] = "zscore"


class TrainConfig(BaseModel):
    k: int = Field(default=5, ge=2, le=10)
    n_iter: int = Field(default=30, ge=1, le=500)
    budget_s: int = Field(default=600, ge=10)
    metric: str = "f1_macro"
    task_type: Literal["classification", "regression"] = "classification"
    auto_feature_select: bool = True
    top_n: int = Field(default=20, ge=2)
    scoring: Literal["f_test", "mutual_info", "variance"] = "f_test"
    seed: int = 42


class TrainState(BaseModel):
    status: str = "idle"  # idle|running|done|cancelled|interrupted|failed
    done: int = 0
    total: int = 0
    current: str = ""
    current_cv_score: Optional[float] = None
    error: str = ""


class Candidate(BaseModel):
    cand_id: int
    model_type: str
    hyperparams: dict
    feature_indices: list[int] = Field(min_length=1)  # 评审 P1-FX-7：防空特征集
    cv_mean: float
    cv_std: float
    metric: str
    is_best: bool = False


class ExportReport(BaseModel):
    ok: bool
    compile: Optional[dict] = None
    predict_consistency: Optional[dict] = None
    feature_consistency: Optional[dict] = None
    static_scan: Optional[dict] = None
    errors: list[str] = Field(default_factory=list)
    compiler: str = ""
    sklearn_version: str = ""
    class_map: list[LabelDef] = Field(default_factory=list)
    code_size_bytes: int = 0
