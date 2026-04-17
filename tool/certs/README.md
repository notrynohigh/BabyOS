# HTTPS 测试证书（固定）

这个目录包含用于本地 HTTPS 测试的自签名证书。

## 文件

- `server.pem` - 自签名证书（CN=localhost, SAN=localhost + 127.0.0.1）
- `server.key` - 对应的私钥

## 重要

这些文件是**固定**的、**已提交到仓库**的，不会运行时重新生成。

证书内容嵌入到 BabyOS 的 `_config/b_mbedtls_config.h` 的
`MBEDTLS_SSL_DEFAULT_CERT` 宏中，匹配这个文件。

## 如果需要重新生成

1. 使用 `openssl` 或 `cryptography` 重新生成证书
2. 提取新证书 PEM 内容（不包含私钥）
3. 更新 `_config/b_mbedtls_config.h` 中的 `MBEDTLS_SSL_DEFAULT_CERT` 宏
4. 重新编译 BabyOS
