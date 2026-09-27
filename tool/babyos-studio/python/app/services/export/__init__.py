"""C 导出器（FR-8）：模型/特征代码生成 + float32 参考实现 + 四项自检。

模块：
- c_common    公共 C 原语（algo_ml_common.h/.c）与 b_os.h stub
- model_cgen  7 种模型的 C 发射器（dt/rf/et/lr/nb/mlp/simple_nn）；xgb/lgbm 暂不支持导出
- feature_cgen 时序 feat_extract 发射器（时域 + 可选 radix-2 FFT）
- reference   float32 参考实现（与 C 同序逐操作对齐，FR-8.5 契约）
- selfcheck   ①编译 ②predict 一致性 ③特征链一致性 ④静态扫描
- generator   bundle 组装 + API 对接
"""
