"""测试 3: 回归 — 多Agent模式（实验者 + 验收者 + 修改者）

数据集：100行，6特征，连续目标值
流程：创建项目 → 导入数据 → 特征工程 → 训练（MSE/R2指标）→ 报告
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
from app.services.automl import sample_candidate, make_estimator, REGRESSION_MODELS
from app.services.metrics_service import metric_value, regression_full_report, is_lower_better
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


def generate_regression_data(n_rows=100, n_features=6):
    """生成回归数据。"""
    rng = np.random.default_rng(42)
    X = rng.standard_normal((n_rows, n_features))
    # 线性关系 + 噪声
    w = rng.standard_normal(n_features)
    y = X @ w + rng.normal(0, 0.5, n_rows)

    cols = [f"feat_{i}" for i in range(n_features)]
    df = pd.DataFrame(X, columns=cols)
    df["target"] = y
    return df


class TestRegression:
    def __init__(self):
        self.pid = None
        self.csv_path = None

    def setup(self):
        print("\n🔧 [实验者] 准备回归数据...")
        df = generate_regression_data()
        self.csv_path = tempfile.mktemp(suffix=".csv")
        df.to_csv(self.csv_path, index=False)
        print(f"  生成 CSV: {self.csv_path} ({len(df)} 行)")

        body = ProjectCreate(name="regression_test", mode="table", task_type="regression")
        meta = svc.create(body)
        self.pid = meta.project_id
        print(f"  创建项目: {self.pid} (mode={meta.mode}, task_type={meta.task_type})")

    def test_import(self):
        print("\n📥 [实验者] 导入数据...")
        meta = svc.get(self.pid)
        with open(self.csv_path, "rb") as f:
            content = f.read()
        uf = MockUploadFile("regression_data.csv", content)
        features = [f"feat_{i}" for i in range(6)]
        mapping = {"features": features, "label_col": "target"}
        result = dataset_service.import_files(meta, [uf], mapping, import_kind="replace")
        check(result["imported"] > 0, f"导入成功: {result['imported']} 个文件")
        meta = svc.get(self.pid)
        check(meta.task_type == "regression", f"任务类型: {meta.task_type}")
        return result

    def test_features(self):
        print("\n📊 [实验者] 特征工程...")
        meta = svc.get(self.pid)
        cfg = feature_service.get_config(meta)
        info = feature_service.compute(meta, cfg)
        check(info["n_samples"] == 100, f"样本数: {info['n_samples']}")
        check(info["n_features"] == 6, f"特征数: {info['n_features']}")

        # 验证 y_float 存在
        m = feature_service.load_matrix(meta)
        check("y_float" in m, "矩阵包含 y_float")
        if "y_float" in m:
            check(m["y_float"].dtype == np.float64, f"y_float dtype: {m['y_float'].dtype}")
            check(np.var(m["y_float"]) > 0, f"y_float 方差 > 0: {np.var(m['y_float']):.4f}")
        return info

    def test_split_regression(self):
        print("\n🔀 [验收者] 验证回归划分...")
        meta = svc.get(self.pid)
        split = feature_service.get_or_create_split(meta, seed=42)
        check("train_idx" in split, "有 train_idx")
        check("test_idx" in split, "有 test_idx")
        check("folds" in split, "有 folds")
        check(len(split["folds"]) >= 2, f"CV 折数: {len(split['folds'])}")
        return split

    def test_train(self):
        print("\n🏋️ [实验者] 开始回归训练...")
        from app.schemas import TrainConfig
        cfg = TrainConfig(
            k=3, n_iter=5, budget_s=120,
            metric="r2", task_type="regression",
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
        print("\n📋 [验收者] 检查回归 leaderboard...")
        lb = training_service.leaderboard(self.pid)
        check(lb["task_type"] == "regression", f"任务类型: {lb['task_type']}")
        check(lb["metric"] == "r2", f"指标: {lb['metric']}")
        check(len(lb["candidates"]) > 0, f"候选数: {len(lb['candidates'])}")

        # R2 越大越好，检查排序是降序
        if len(lb["candidates"]) >= 2:
            check(lb["candidates"][0]["cv_mean"] >= lb["candidates"][1]["cv_mean"],
                  f"排序正确（降序）: {lb['candidates'][0]['cv_mean']:.4f} >= {lb['candidates'][1]['cv_mean']:.4f}")
        return lb

    def test_report(self):
        print("\n📋 [验收者] 检查回归报告...")
        report = training_service.get_report(self.pid)
        check(report["task_type"] == "regression", f"报告任务类型")
        mt = report["best_model"]["metrics_test"]
        check("mse" in mt, "包含 MSE")
        check("mae" in mt, "包含 MAE")
        check("r2" in mt, "包含 R2")
        check("mape" in mt, "包含 MAPE")
        check("residual_mean" in mt, "包含残差均值")
        check("residual_std" in mt, "包含残差标准差")
        if mt.get("r2") is not None:
            check(mt["r2"] > -1, f"R2 > -1: {mt['r2']:.4f}")
        return report

    def test_regression_metrics(self):
        print("\n🔍 [验收者] 验证回归指标...")
        rng = np.random.default_rng(42)
        y_true = rng.standard_normal(100)
        y_pred = y_true + rng.normal(0, 0.1, 100)

        report = regression_full_report(y_true, y_pred)
        check(report["mse"] < 0.1, f"MSE < 0.1: {report['mse']:.4f}")
        check(report["mae"] < 0.3, f"MAE < 0.3: {report['mae']:.4f}")
        check(report["r2"] > 0.9, f"R2 > 0.9: {report['r2']:.4f}")
        check(is_lower_better("mse"), "mse 越小越好")
        check(not is_lower_better("r2"), "r2 越大越好")

    def test_regression_model_pool(self):
        print("\n🔍 [验收者] 验证回归模型池...")
        rng = np.random.default_rng(77)
        used = set()
        for i in range(30):
            cand = sample_candidate(rng, i, task_type="regression")
            used.add(cand["model_type"])
        check(len(used) >= 4, f"覆盖 {len(used)} 种回归模型: {sorted(used)}")
        for mt in REGRESSION_MODELS:
            if mt not in ("xgb_r", "lgbm_r"):  # 可能未安装
                check(mt in used, f"模型 {mt} 在搜索空间中")

    def test_regression_estimator(self):
        print("\n🔍 [验收者] 验证回归 estimator...")
        rng = np.random.default_rng(42)
        X = rng.standard_normal((50, 6))
        y = rng.standard_normal(50)
        for mt in ["dt_r", "rf_r", "et_r", "lr_r", "simple_nn_r"]:
            try:
                est = make_estimator(mt, {}, rng)
                est.fit(X, y)
                pred = est.predict(X[:5])
                check(pred.shape == (5,), f"{mt} 预测形状正确")
                check(np.all(np.isfinite(pred)), f"{mt} 预测值有限")
            except Exception as e:
                check(False, f"{mt} 异常: {e}")

    def cleanup(self):
        print("\n🧹 [修改者] 清理...")
        if self.csv_path and os.path.exists(self.csv_path):
            os.unlink(self.csv_path)
        if self.pid:
            svc.delete(self.pid, hard=True)
            print(f"  已删除项目: {self.pid}")


def run():
    print("=" * 60)
    print("TEST 3: 回归 — 多Agent全流程测试")
    print("=" * 60)
    t = TestRegression()
    try:
        t.setup()
        t.test_import()
        t.test_features()
        t.test_split_regression()
        t.test_regression_metrics()
        t.test_regression_model_pool()
        t.test_regression_estimator()
        t.test_train()
        t.test_leaderboard()
        t.test_report()
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
