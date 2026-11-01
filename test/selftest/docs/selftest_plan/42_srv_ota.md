# 模块 42：OTA Service

**文件**: `bos/services/b_srv_ota.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

OTA Service 是 IAP 的上层服务封装，通过 Protocol Service 接收固件帧。

### 1.2 核心 API

```c
// 初始化
int bOtaSrvInit(bProtSrvId_t protocol_id, bOtaSrvSendData_t send,
                uint32_t cache_dev_no, uint32_t backup_dev_no, uint32_t backup_time_s);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bOtaSrvInit` | 正常初始化 | 返回 0 |
| `test_bOtaSrvGetStatus` | 查询 OTA 状态 | status 有效值 |
| `test_bOtaSrvBackupValid` | 检查备份有效性 | valid=0 或 1 |
| `test_bOtaSrvPercentage` | 查询接收进度 | pct<=100 |

---

## 三、测试文件

```
test/selftest/test_service.c
```

**依赖**:
- `bos/services/b_srv_ota.c`
- `bos/modules/inc/b_mod_iap.h`
- `_OTA_SERVICE_ENABLE=1`

---

## 四、备注

OTA Service 测试需要预先设置 IAP mock 状态。