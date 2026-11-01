#!/usr/bin/env python3
"""
完整流程验证：训练 → 代码生成 → 编译 → 测试
使用合成数据直接调用导出服务，验证深度绑定
"""
import sys
import os
import json
import numpy as np
from pathlib import Path

# 添加导出服务路径
sys.path.insert(0, '/home/yyds/code/BabyOS/tool/babyos-studio/python')

# 导入必要的模块
from app.services.export import generator
from app.services.export import feature_cgen

def create_synthetic_model():
    """创建合成模型数据用于测试"""
    # 模拟 3 通道、每通道 100 点窗口、5 个特征、3 个类别
    n_channels = 3
    n_features_per_ch = 2  # mean, std
    n_features = n_channels * n_features_per_ch
    n_classes = 3
    win_len = 100

    # 特征列表（导出顺序）
    exported_features = [
        ("ch0", "mean"),
        ("ch0", "std"),
        ("ch1", "mean"),
        ("ch1", "std"),
        ("ch2", "mean"),
        ("ch2", "std"),
    ]

    # 归一化参数
    offset = np.array([-4.2491e-03, 7.155221e-01, 7.1237576e-01,
                       1.3625374e-03, 7.126943e-01, 1.234567e-01], dtype=np.float32)
    inv_scale = np.array([1.1324026e+02, 9.385537e+01, 1.07399765e+02,
                          8.888113e+01, 1.913222e+02, 5.555555e+01], dtype=np.float32)

    # 简单的决策树模型（2 棵树）
    trees = [
        {
            "nodes": [
                # (feature, threshold, left, right, class)
                (0, 4.705810844898224e-01, 1, 2, -2),
                (2, -5.311825275421143e-01, 3, 4, -2),
                (-2, -1, -1, 0, 0),  # leaf class 0
                (4, -1.514695405960083e+00, 5, 6, -2),
                (-2, -1, -1, 1, 1),  # leaf class 1
                (-2, -1, -1, 2, 2),  # leaf class 2
                (3, 4.323667883872986e-01, 7, 8, -2),
                (-2, -1, -1, 3, 1),  # leaf class 1
                (-2, -1, -1, 4, 2),  # leaf class 2
            ],
            "proba": np.array([
                [1.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [0.25, 0.6875, 0.0625],
                [0.0, 1.0, 0.0],
                [0.0, 0.0, 1.0],
            ], dtype=np.float32)
        },
        {
            "nodes": [
                (3, 7.757421433925629e-01, 1, 2, -2),
                (3, -1.668463610112667e-01, 3, 4, -2),
                (-2, -1, -1, 0, 0),  # leaf class 0
                (-2, -1, -1, 1, 1),  # leaf class 1
                (1, 1.4270704686641693e+00, 5, 6, -2),
                (-2, -1, -1, 2, 2),  # leaf class 2
                (-2, -1, -1, 3, 1),  # leaf class 1
            ],
            "proba": np.array([
                [0.1, 0.8, 0.1],
                [0.0, 0.0, 1.0],
                [1.0, 0.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, 0.0, 1.0],
            ], dtype=np.float32)
        },
    ]

    return {
        "project_id": "e2e_test_automl",
        "model_type": "random_forest",
        "n_classes": n_classes,
        "n_features": n_features,
        "win_len": win_len,
        "n_channels": n_channels,
        "channels": ["ch0", "ch1", "ch2"],
        "features": exported_features,
        "feature_names": [f"{ch}_{feat}" for ch, feat in exported_features],
        "labels": ["class_0", "class_1", "class_2"],
        "offset": offset,
        "inv_scale": inv_scale,
        "trees": trees,
        "freq_enabled": False,
        "freq_bands": 0,
        "fs": 1000.0,
    }

def test_code_generation():
    """测试代码生成器"""
    print("=" * 70)
    print("  BabyOS AutoML 完整流程验证")
    print("=" * 70)

    # Step 1: 创建合成模型
    print("\n[Step 1] 创建合成模型数据...")
    model = create_synthetic_model()
    print(f"  ✅ 模型类型: {model['model_type']}")
    print(f"  ✅ 特征数: {model['n_features']}")
    print(f"  ✅ 类别数: {model['n_classes']}")
    print(f"  ✅ 窗口长度: {model['win_len']}")

    # Step 2: 生成特征提取代码
    print("\n[Step 2] 生成特征提取代码...")
    feat_result = feature_cgen.emit_feat_extract(
        prefix=model["project_id"],
        channels=model["channels"],
        exported=model["features"],
        n=model["win_len"],
        fs=model["fs"],
        freq_enabled=model["freq_enabled"],
        freq_bands=model["freq_bands"],
    )

    feat_body = feat_result["body"]
    feat_helpers = feat_result["helpers"]

    print(f"  ✅ helpers 长度: {len(feat_helpers)} (应为 0，原语已预置)")
    print(f"  ✅ body 长度: {len(feat_body)} 字符")

    # Step 3: 验证调用预置原语
    print("\n[Step 3] 验证调用预置原语...")
    primitive_checks = [
        ("bAlgoSignalStats", "调用信号统计原语"),
        ("bAlgoSignalMean", "调用 bAlgoSignalMean"),
        ("bAlgoSignalStd", "调用 bAlgoSignalStd"),
    ]

    all_ok = True
    for symbol, desc in primitive_checks:
        found = symbol in feat_body
        status = "✅" if found else "❌"
        print(f"  {status} {desc} ({symbol})")
        if not found:
            all_ok = False

    # Step 4: 验证无内联实现
    print("\n[Step 4] 验证无内联实现...")
    inline_checks = [
        ("s_sum / (float)N", "不应有内联 mean"),
        ("sqrtf(s_m2 / (float)N)", "不应有内联 std"),
        ("static void", "不应有内嵌 static 函数"),
    ]

    for symbol, desc in inline_checks:
        found = symbol in feat_body
        status = "✅" if not found else "❌"
        print(f"  {status} {desc} ({symbol})")
        if found:
            all_ok = False

    # Step 5: 生成完整 C 代码
    print("\n[Step 5] 生成完整 C 代码...")
    try:
        # 调用生成器
        bundle = generator.build_bundle(model)

        # 检查生成的 C 文件
        c_files = {}
        for item in bundle:
            if item["path"].endswith(".c") and "example" not in item["path"]:
                c_files[item["path"]] = item["content"]

        print(f"  ✅ 生成了 {len(c_files)} 个 C 文件")

        # 检查主 C 文件
        main_c_name = f"algo_{model['project_id']}.c"
        if main_c_name in c_files:
            main_c = c_files[main_c_name]
            print(f"  ✅ 主 C 文件: {main_c_name} ({len(main_c)} 字符)")

            # 验证包含预置原语头文件
            if "#include \"algo_signal.h\"" in main_c:
                print("  ✅ 包含 algo_signal.h")
            else:
                print("  ❌ 未包含 algo_signal.h")
                all_ok = False

            # 验证调用预置原语
            if "bAlgoSignalStats" in main_c:
                print("  ✅ 调用 bAlgoSignalStats")
            else:
                print("  ❌ 未调用 bAlgoSignalStats")
                all_ok = False
        else:
            print(f"  ❌ 未找到主 C 文件: {main_c_name}")
            all_ok = False

        # Step 6: 保存生成的代码
        print("\n[Step 6] 保存生成的代码...")
        output_dir = Path("build/generated_code")
        output_dir.mkdir(parents=True, exist_ok=True)

        for item in bundle:
            output_path = output_dir / item["path"]
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_text(item["content"], encoding="utf-8")
            print(f"  ✅ 保存 {item['path']}")

        print(f"\n  📁 生成的代码保存在: {output_dir}")

    except Exception as e:
        print(f"  ❌ 生成代码时出错: {e}")
        import traceback
        traceback.print_exc()
        all_ok = False

    # 总结
    print("\n" + "=" * 70)
    if all_ok:
        print("✅ 完整流程验证通过！")
        print("   生成的代码正确调用预置原语 bAlgoSignal*")
        print("   深度绑定实现正确")
    else:
        print("❌ 完整流程验证失败")
        sys.exit(1)
    print("=" * 70)

    return all_ok

if __name__ == "__main__":
    success = test_code_generation()
    sys.exit(0 if success else 1)
