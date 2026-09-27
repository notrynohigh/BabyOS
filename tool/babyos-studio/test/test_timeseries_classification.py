"""测试 1: 时序分类 — 多Agent模式（实验者 + 验收者 + 修改者）

数据集：6通道传感器数据，100样本，3类（walking/sitting/running），10000行
流程：创建项目 → 导入数据 → 标注 → 特征工程 → 训练 → 报告 → 导出
"""
from __future__ import annotations

import os
import sys
import time
import tempfile
import shutil

import numpy as np
import pandas as pd

# 确保可以导入 app 模块
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.schemas import ProjectCreate
from app.services.project_service import ProjectService
from app.services import feature_service, training_service, dataset_service
from app.services.automl import sample_candidate, make_estimator, CLASSIFICATION_MODELS
from app.services.metrics_service import metric_value, full_report
from app.deps import project_dir, atomic_write_json, save_meta, read_json


class MockUploadFile:
    """模拟 FastAPI UploadFile。"""
    def __init__(self, filename: str, content: bytes):
        import io
        self.filename = filename
        self.file = io.BytesIO(content)
        self.size = len(content)

svc = ProjectService()
PASS = 0
FAIL = 0


def check(condition: bool, msg: str):
    global PASS, FAIL
    if condition:
        PASS += 1
        print(f"  ✅ {msg}")
    else:
        FAIL += 1
        print(f"  ❌ {msg}")


def generate_ts_data(n_rows=10000, n_channels=6, fs=100.0):
    """生成时序传感器数据 CSV（含 timestamp + 6 通道 + label 列）。"""
    rng = np.random.default_rng(42)
    t = np.arange(n_rows) / fs
    channels = {}
    for i in range(n_channels):
        # 每个通道用不同频率的正弦波 + 噪声
        freq = 1.0 + i * 0.5
        channels[f"ch{i}"] = np.sin(2 * np.pi * freq * t) + rng.normal(0, 0.1, n_rows)

    # label: 3 类，每类约 1/3
    label = np.zeros(n_rows, dtype=int)
    seg = n_rows // 3
    label[:seg] = 0  # walking
    label[seg:2*seg] = 1  # sitting
    label[2*seg:] = 2  # running

    df = pd.DataFrame({"timestamp": t, **channels, "label": label})
    return df


class TestTimeseriesClassification:
    """时序分类全流程测试（实验者角色）。"""

    def __init__(self):
        self.pid = None
        self.csv_path = None

    def setup(self):
        """生成测试数据并创建项目。"""
        print("\n🔧 [实验者] 准备测试数据...")
        # 生成 CSV
        df = generate_ts_data()
        self.csv_path = tempfile.mktemp(suffix=".csv")
        df.to_csv(self.csv_path, index=False)
        print(f"  生成 CSV: {self.csv_path} ({len(df)} 行)")

        # 创建项目
        body = ProjectCreate(name="ts_cls_test", mode="timeseries", task_type="classification",
                             sampling_rate=100.0)
        meta = svc.create(body)
        self.pid = meta.project_id
        print(f"  创建项目: {self.pid} (mode={meta.mode}, task_type={meta.task_type})")

    def test_import(self):
        """实验者: 导入数据。"""
        print("\n📥 [实验者] 导入数据...")
        meta = svc.get(self.pid)
        with open(self.csv_path, "rb") as f:
            content = f.read()
        uf = MockUploadFile("ts_data.csv", content)
        mapping = {"channels": [f"ch{i}" for i in range(6)], "ts_col": "timestamp", "label_col": "label"}
        result = dataset_service.import_files(meta, [uf], mapping, import_kind="replace")
        check(result["imported"] > 0, f"导入成功: {result['imported']} 个文件")
        meta = svc.get(self.pid)
        check(len(meta.channels) > 0, f"通道已识别: {meta.channels}")
        check(meta.sampling_rate == 100.0, f"采样率正确: {meta.sampling_rate}")
        return result

    def test_label(self):
        """验收者: 验证标注（导入时已自动创建）。"""
        print("\n🏷️ [验收者] 验证标注...")
        meta = svc.get(self.pid)
        check(len(meta.labels) > 0, f"标签已创建: {[l.name for l in meta.labels]}")

        # 验证片段已自动创建（RLE 方式）
        segs = read_json(project_dir(self.pid) / "labeling" / "segments.json", default=[])
        check(len(segs) > 0, f"片段已自动创建: {len(segs)} 个")
        labeled_segs = [s for s in segs if s.get("label_id", -1) >= 0]
        check(len(labeled_segs) > 0, f"有标注片段: {len(labeled_segs)} 个")
        return segs

    def test_features(self):
        """实验者: 特征工程（验证默认特征勾选）。"""
        print("\n📊 [实验者] 特征工程...")
        meta = svc.get(self.pid)
        cfg = feature_service.get_config(meta)

        # 验证默认 channel_features 已自动填充
        check(cfg.channel_features is not None, "channel_features 已自动填充")
        if cfg.channel_features:
            for ch in meta.channels:
                check(ch in cfg.channel_features, f"通道 {ch} 有默认特征映射")
                if ch in cfg.channel_features:
                    feats = cfg.channel_features[ch]
                    check("mean" in feats, f"  {ch} 包含 mean")
                    check("std" in feats, f"  {ch} 包含 std")
                    check("rms" in feats, f"  {ch} 包含 rms")
                    check("ptp" in feats, f"  {ch} 包含 ptp")
                    check("zcr" in feats, f"  {ch} 包含 zcr")

        # 计算特征矩阵
        info = feature_service.compute(meta, cfg)
        check(info["n_samples"] > 0, f"特征矩阵: {info['n_samples']} 样本, {info['n_features']} 特征")
        return info

    def test_train(self):
        """实验者: 训练。"""
        print("\n🏋️ [实验者] 开始训练...")
        from app.schemas import TrainConfig
        cfg = TrainConfig(
            k=3, n_iter=5, budget_s=120,
            metric="f1_macro", task_type="classification",
            auto_feature_select=False, seed=42,
        )
        result = training_service.start(self.pid, cfg)
        check(result["status"] == "running", f"训练已启动: {result['status']}")

        # 等待训练完成
        for _ in range(60):
            time.sleep(2)
            st = training_service.status(self.pid)
            if st["status"] != "running":
                break
        check(st["status"] == "done", f"训练完成: {st['status']}")
        return st

    def test_leaderboard(self):
        """验收者: 检查 leaderboard。"""
        print("\n📋 [验收者] 检查 leaderboard...")
        lb = training_service.leaderboard(self.pid)
        check(lb["metric"] == "f1_macro", f"指标正确: {lb['metric']}")
        check(lb["task_type"] == "classification", f"任务类型正确: {lb['task_type']}")
        check(len(lb["candidates"]) > 0, f"有候选模型: {len(lb['candidates'])}")
        if lb["candidates"]:
            best = lb["candidates"][0]
            check(best["cv_mean"] > 0, f"最佳 CV 分数 > 0: {best['cv_mean']:.4f}")
        return lb

    def test_report(self):
        """验收者: 检查训练报告。"""
        print("\n📋 [验收者] 检查训练报告...")
        report = training_service.get_report(self.pid)
        check(report["task_type"] == "classification", f"报告任务类型: {report['task_type']}")
        check(report["summary"]["total_candidates"] > 0, f"候选数: {report['summary']['total_candidates']}")
        check(report["summary"]["best_model_type"] is not None, f"最佳模型: {report['summary']['best_model_type']}")
        check(report["best_model"]["metrics_test"] != {}, "测试指标已生成")
        return report

    def test_model_candidates(self):
        """验收者: 验证模型池覆盖率。"""
        print("\n🔍 [验收者] 验证模型池...")
        rng = np.random.default_rng(99)
        used_models = set()
        for i in range(20):
            cand = sample_candidate(rng, i, task_type="classification")
            used_models.add(cand["model_type"])
        check(len(used_models) >= 5, f"覆盖了 {len(used_models)} 种分类模型: {sorted(used_models)}")
        for mt in CLASSIFICATION_MODELS:
            check(mt in used_models or mt in ("xgb", "lgbm"), f"模型 {mt} 在搜索空间中")

    def cleanup(self):
        """清理测试项目。"""
        print("\n🧹 [修改者] 清理...")
        if self.csv_path and os.path.exists(self.csv_path):
            os.unlink(self.csv_path)
        if self.pid:
            svc.delete(self.pid, hard=True)
            print(f"  已删除项目: {self.pid}")


def run():
    print("=" * 60)
    print("TEST 1: 时序分类 — 多Agent全流程测试")
    print("=" * 60)
    t = TestTimeseriesClassification()
    try:
        t.setup()
        t.test_import()
        t.test_label()
        t.test_features()
        t.test_train()
        t.test_leaderboard()
        t.test_report()
        t.test_model_candidates()
    except Exception as e:
        global FAIL
        FAIL += 1
        print(f"\n  ❌ 异常: {e}")
        import traceback
        traceback.print_exc()
    finally:
        t.cleanup()

    print(f"\n{'=' * 60}")
    print(f"结果: {PASS} 通过, {FAIL} 失败")
    print(f"{'=' * 60}")
    return FAIL == 0


if __name__ == "__main__":
    ok = run()
    sys.exit(0 if ok else 1)
