"""测试 2: 表格分类 — 多Agent模式（实验者 + 验收者 + 修改者）

数据集：100行，8特征，4类
流程：创建项目 → 导入数据 → 特征工程 → 训练 → 报告 → 导出
"""
from __future__ import annotations

import os
import sys
import time
import tempfile

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.schemas import ProjectCreate
from app.services.project_service import ProjectService
from app.services import feature_service, training_service, dataset_service
from app.services.automl import sample_candidate, CLASSIFICATION_MODELS
from app.deps import project_dir, atomic_write_json, save_meta


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


def generate_table_data(n_rows=100, n_features=8, n_classes=4):
    """生成表格分类数据。"""
    rng = np.random.default_rng(42)
    X = rng.standard_normal((n_rows, n_features))
    # 4 类，用线性组合分隔
    w = rng.standard_normal((n_features, n_classes))
    logits = X @ w
    y = np.argmax(logits, axis=1) + rng.choice([-1, 0, 1], size=n_rows, p=[0.05, 0.9, 0.05])
    y = np.clip(y, 0, n_classes - 1)

    cols = [f"feat_{i}" for i in range(n_features)]
    df = pd.DataFrame(X, columns=cols)
    df["label"] = y
    return df


class TestTableClassification:
    def __init__(self):
        self.pid = None
        self.csv_path = None

    def setup(self):
        print("\n🔧 [实验者] 准备表格分类数据...")
        df = generate_table_data()
        self.csv_path = tempfile.mktemp(suffix=".csv")
        df.to_csv(self.csv_path, index=False)
        print(f"  生成 CSV: {self.csv_path} ({len(df)} 行, {len(df.columns)} 列)")

        body = ProjectCreate(name="tbl_cls_test", mode="table", task_type="classification")
        meta = svc.create(body)
        self.pid = meta.project_id
        print(f"  创建项目: {self.pid} (mode={meta.mode}, task_type={meta.task_type})")

    def test_import(self):
        print("\n📥 [实验者] 导入数据...")
        meta = svc.get(self.pid)
        with open(self.csv_path, "rb") as f:
            content = f.read()
        uf = MockUploadFile("table_data.csv", content)
        features = [f"feat_{i}" for i in range(8)]
        mapping = {"features": features, "label_col": "label"}
        result = dataset_service.import_files(meta, [uf], mapping, import_kind="replace")
        check(result["imported"] > 0, f"导入成功: {result['imported']} 个文件")
        meta = svc.get(self.pid)
        check(len(meta.channels) > 0, f"特征列: {meta.channels}")
        return result

    def test_features(self):
        print("\n📊 [实验者] 特征工程...")
        meta = svc.get(self.pid)
        cfg = feature_service.get_config(meta)
        check(cfg.feature_ids == [], "表格模式 feature_ids 为空（使用全部列）")

        info = feature_service.compute(meta, cfg)
        check(info["n_samples"] == 100, f"样本数: {info['n_samples']}")
        check(info["n_features"] == 8, f"特征数: {info['n_features']}")
        return info

    def test_train(self):
        print("\n🏋️ [实验者] 开始训练...")
        from app.schemas import TrainConfig
        cfg = TrainConfig(
            k=3, n_iter=5, budget_s=120,
            metric="f1_macro", task_type="classification",
            auto_feature_select=False, seed=42,
        )
        result = training_service.start(self.pid, cfg)
        check(result["status"] == "running", f"训练已启动")

        for _ in range(60):
            time.sleep(2)
            st = training_service.status(self.pid)
            if st["status"] != "running":
                break
        check(st["status"] == "done", f"训练完成: {st['status']}")
        return st

    def test_leaderboard(self):
        print("\n📋 [验收者] 检查 leaderboard...")
        lb = training_service.leaderboard(self.pid)
        check(lb["task_type"] == "classification", f"任务类型: {lb['task_type']}")
        check(len(lb["candidates"]) > 0, f"候选数: {len(lb['candidates'])}")
        if lb["candidates"]:
            check(lb["candidates"][0]["cv_mean"] > 0, f"最佳 CV: {lb['candidates'][0]['cv_mean']:.4f}")
        return lb

    def test_report(self):
        print("\n📋 [验收者] 检查训练报告...")
        report = training_service.get_report(self.pid)
        check(report["task_type"] == "classification", f"报告任务类型")
        check("accuracy" in report["best_model"]["metrics_test"], "测试指标包含 accuracy")
        check("confusion_matrix" in report["best_model"]["metrics_test"], "包含混淆矩阵")
        return report

    def test_model_pool(self):
        print("\n🔍 [验收者] 验证分类模型池...")
        rng = np.random.default_rng(88)
        used = set()
        for i in range(30):
            cand = sample_candidate(rng, i, task_type="classification")
            used.add(cand["model_type"])
        check(len(used) >= 5, f"覆盖 {len(used)} 种模型: {sorted(used)}")

    def cleanup(self):
        print("\n🧹 [修改者] 清理...")
        if self.csv_path and os.path.exists(self.csv_path):
            os.unlink(self.csv_path)
        if self.pid:
            svc.delete(self.pid, hard=True)
            print(f"  已删除项目: {self.pid}")


def run():
    print("=" * 60)
    print("TEST 2: 表格分类 — 多Agent全流程测试")
    print("=" * 60)
    t = TestTableClassification()
    try:
        t.setup()
        t.test_import()
        t.test_features()
        t.test_train()
        t.test_leaderboard()
        t.test_report()
        t.test_model_pool()
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
