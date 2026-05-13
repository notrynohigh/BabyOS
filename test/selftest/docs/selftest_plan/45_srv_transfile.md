# 模块 45：TransFile 文件传输服务

**文件**: `bos/services/b_srv_transfile.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

TransFile 服务实现基于 Protocol Service 的文件传输协议。

### 1.2 核心 API

```c
// 初始化
int bTFLSrvInit(bProtSrvId_t protocol_id, bTFLSrvSendData_t send);

// 反初始化
int bTFLSrvDeinit(void);

// 获取文件信息
int bTFLSrvGetFileInfo(uint32_t dev_no, uint32_t base_addr,
                       const char *filename, bTFLSrvFileInfo_t *pinfo);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bTFLSrvInit` | 正常初始化 | 返回 0 |
| `test_bTFLSrvGetFileInfo` | 获取文件信息 | ret=0 或 -1 |
| `test_bTFLSrvDeinit` | 正常反初始化 | 返回 0 |

---

## 三、测试文件

```
test/selftest/test_service.c
```

**依赖**:
- `bos/services/b_srv_transfile.c`
- `bos/services/inc/b_srv_transfile.h`
- `_TRANSFILE_SERVICE_ENABLE=1`

---

## 四、备注

TransFile 测试使用 xmodem128 协议。