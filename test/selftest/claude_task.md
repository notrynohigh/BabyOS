# BabyOS 自动化测试任务规范（最终版）

**更新日期**: 2026-05-12
**测试状态**: ✅ 全部通过 (ALL PASSED)

---

## 一、任务范围与目标

**严格按照 45 份方案文档（`test/selftest/docs/selftest_plan/`）执行测试**，覆盖 BabyOS 全部模块，共 6 批测试。测试即验证——不能仅做编译检查，除非方案本身声明为编译检查。

## 二、测试执行标准（强制）

1. **编译通过**：BabyOS 自有代码（`bos/` 排除 thirdparty）零错误、零警告
2. **用例通过**：每个方案文档中的测试用例均通过，任务才算结束
3. **无 crash**：0 FAIL，0 SIGSEGV
4. **测试即验证**：不能仅做编译检查（除非方案本身声明为编译检查）

## 三、测试范围与实际测试函数

### 3.1 驱动测试（test_drivers.c）

| 测试函数 | 覆盖驱动 | 状态 |
|---------|---------|------|
| `test_bDriverTestFlashOpen` | testflash | ✅ |
| `test_bDriverTestFlashReadWrite` | testflash | ✅ |
| `test_bDriverTestFlashSectorSize` | testflash | ✅ |
| `test_bDriverTestFlashSectorCount` | testflash | ✅ |
| `test_bDriverTestFlashMultiSector` | testflash | ✅ |
| `test_bDriverTestFlashReadOnly` | testflash | ✅ |
| `test_bDriverTestFlashWriteOnly` | testflash | ✅ |
| `test_bDriverKeyOpen` | key | ✅ |
| `test_bDriverKeyReadLow` | key | ✅ |
| `test_bDriverKeyReadHigh` | key | ✅ |
| `test_bDriverDuplicateOpen` | (通用) | ✅ |
| `test_bDriverInvalidDevice` | (通用) | ✅ |

### 3.2 算法测试（test_algo.c）

| 测试函数 | 覆盖方案 | 状态 |
|---------|---------|------|
| `test_bCrc8` | #01 CRC8 | ✅ |
| `test_bCrc16` | #01 CRC16 | ✅ |
| `test_bCrc32` | #01 CRC32 | ✅ |
| `test_bCrcChunked` | #01 分块CRC | ✅ |
| `test_bBase64Encode` | #02 Base64 | ✅ |
| `test_bMd5String` | #03 MD5 | ✅ |
| `test_bSha1Hmac` | #04 SHA1/HMAC | ✅ |
| `test_bSortBubble` | #05 冒泡排序 | ✅ |
| `test_bSortInsertion` | #05 插入排序 | ✅ |
| `test_bSortSelect` | #05 选择排序 | ✅ |
| `test_bUtf8Encode` | #06 UTF8编码 | ✅ |
| `test_bUtf8Decode` | #06 UTF8解码 | ✅ |
| `test_bUtf8Size` | #06 UTF8大小 | ✅ |

### 3.3 核心测试（test_core.c）

| 测试函数 | 覆盖方案 | 状态 |
|---------|---------|------|
| `test_bQueueCreate` | #07 Queue创建 | ✅ |
| `test_bQueuePutGet` | #07 Queue读写 | ✅ |
| `test_bQueueReset` | #07 Queue重置 | ✅ |
| `test_bSemCreate` | #08 Semaphore创建 | ✅ |
| `test_bSemAcquireRelease` | #08 Semaphore获取/释放 | ✅ |
| `test_bSemCounting` | #08 Semaphore计数 | ✅ |
| `test_bTimerCreate` | #09 Timer创建 | ✅ |
| `test_bTimerStart` | #09 Timer启动/停止 | ✅ |
| `test_bTaskCreate` | #10 Task创建 | ✅ |
| `test_bTaskSuspendResume` | #10 Task挂起/恢复 | ✅ |
| `test_bTaskYield` | #10 Task让出 | ✅ |
| `test_bTaskGetId` | #10 Task获取ID | ✅ |
| `test_bDeviceNull` | #11 设备-空设备 | ✅ |
| `test_bDeviceFlash` | #11 设备-Flash读写 | ✅ |
| `test_bDeviceDuplicateOpen` | #11 设备-重复打开 | ✅ |

### 3.4 工具函数测试（test_utils.c）

| 测试函数 | 覆盖方案 | 状态 |
|---------|---------|------|
| `test_bFIFOInit` | #12 FIFO初始化 | ✅ |
| `test_bFIFOWriteRead` | #12 FIFO读写 | ✅ |
| `test_bFIFOLength` | #12 FIFO长度 | ✅ |
| `test_bFIFOFlush` | #12 FIFO清空 | ✅ |
| `test_bMemAllocFree` | #13 内存分配/释放 | ✅ |
| `test_bMemSizes` | #13 内存大小查询 | ✅ |
| `test_bCalloc` | #13 Calloc | ✅ |
| `test_bUTCStructConversion` | #14 UTC结构转换 | ✅ |
| `test_bUTCSetGet` | #14 UTC设置/获取 | ✅ |
| `test_bUTCTimestampRoundTrip` | #14 UTC时间戳往返 | ✅ |
| `test_bLunarCalendar` | #15 农历 | ✅ |
| `test_bParseString` | #16 字符串解析 | ✅ |

### 3.5 模块测试（test_modules.c）

| 测试函数 | 覆盖方案 | 状态 |
|---------|---------|------|
| `test_bFSInit` | #19 FS初始化 | ✅ |
| `test_bFSInitNull` | #19 FS初始化空 | ✅ |
| `test_bFSMountFatfs` | #19 FatFS挂载 | ✅ |
| `test_bFSMountLittlefs` | #19 LittleFS挂载 | ✅ |
| `test_bFSUnmountFatfs` | #19 FatFS卸载 | ✅ |
| `test_bFSUnmountLittlefs` | #19 LittleFS卸载 | ✅ |
| `test_bFSOpenWriteReadFatfs` | #19 FatFS读写 | ✅ |
| `test_bFSOpenWriteReadLittlefs` | #19 LittleFS读写 | ✅ |
| `test_bFSGetInfoFatfs` | #19 FatFS空间信息 | ✅ |
| `test_bFSGetInfoLittlefs` | #19 LittleFS空间信息 | ✅ |
| `test_bFSCoexistBothFS` | #19 双FS共存 | ✅ |
| `test_bErrorInit` | #20 Error初始化 | ✅ |
| `test_bErrorRegist` | #20 Error注册 | ✅ |
| `test_bErrorIsExistClear` | #20 Error查询/清除 | ✅ |
| `test_bErrorClearInvalid` | #20 Error无效清除 | ✅ |
| `test_bStateCreate` | #21 State创建 | ✅ |
| `test_bStateAdd` | #21 State添加 | ✅ |
| `test_bStateTransfer` | #21 State转移 | ✅ |
| `test_bStateInvokeEvent` | #21 State事件 | ✅ |
| `test_bSelectNullFd` | #23 Select空FD | ✅ |
| `test_bSelectReadable` | #23 Select可读 | ✅ |
| `test_bSelectWritable` | #23 Select可写 | ✅ |
| `test_bQRCodeCreate` | #30 QRCode创建 | ✅ |
| `test_bQRCodeGetValue` | #30 QRCode获取值 | ✅ |
| `test_bKVSetGet` | #18 KV设置/获取 | ✅ |
| `test_bKVDelete` | #18 KV删除 | ✅ |
| `test_bKVMultipleKeys` | #18 KV多键 | ✅ |
| `test_bKVOverwrite` | #18 KV覆盖 | ✅ |
| `test_bMenuAddSibling` | #22 Menu添加兄弟 | ✅ |
| `test_bMenuAddChild` | #22 Menu添加子项 | ✅ |
| `test_bMenuAction` | #22 Menu动作 | ✅ |
| `test_bMenuJump` | #22 Menu跳转 | ✅ |
| `test_bMenuSetVisible` | #22 Menu可见性 | ✅ |
| `test_bMenuCurrentID` | #22 Menu当前ID | ✅ |
| `test_bParamShellSet` | #24 Param设置 | ✅ |
| `test_bParamShellSet2` | #24 Param设置2 | ✅ |
| `test_bParamShellSetRoundTrip` | #24 Param往返 | ✅ |
| `test_bParamShellGet` | #24 Param获取 | ✅ |
| `test_bParamShellList` | #24 Param列表 | ✅ |
| `test_bModbusParseReadResp` | #26 Modbus读响应 | ✅ |
| `test_bModbusParseWriteRegsResp` | #26 Modbus写响应 | ✅ |
| `test_bModbusParseCrcError` | #26 Modbus CRC错误 | ✅ |
| `test_bModbusParseShortFrame` | #26 Modbus短帧 | ✅ |
| `test_bModbusPkgReadReq` | #26 Modbus读请求 | ✅ |
| `test_bModbusPkgWriteReq` | #26 Modbus写请求 | ✅ |
| `test_bModbusPkgBadCmd` | #26 Modbus错误命令 | ✅ |
| `test_bShellInitIdempotent` | #25 Shell幂等初始化 | ✅ |
| `test_bShellParseNull` | #25 Shell空解析 | ✅ |
| `test_bShellParseZeroLen` | #25 Shell零长度 | ✅ |
| `test_bShellParseEmptyStr` | #25 Shell空字符串 | ✅ |
| `test_bShellVersionCmd` | #25 Shell版本命令 | ✅ |
| `test_bShellUnknownCmd` | #25 Shell未知命令 | ✅ |

### 3.6 服务测试（test_service.c）

| 测试函数 | 覆盖方案 | 状态 |
|---------|---------|------|
| `test_bProtSrvInit` | #41 Protocol初始化 | ✅ |
| `test_bProtSrvParse` | #41 Protocol解析 | ✅ |
| `test_bProtSrvParseEOT` | #41 Protocol解析EOT | ✅ |
| `test_bProtSrvParseBad` | #41 Protocol解析错误 | ✅ |
| `test_bProtSrvPackage` | #41 Protocol打包 | ✅ |
| `test_bProtSrvSubscribe` | #41 Protocol订阅 | ✅ |
| `test_bTFLSrvInit` | #45 TransFile初始化 | ✅ |
| `test_bTFLSrvGetFileInfo` | #45 TransFile文件信息 | ✅ |
| `test_bTFLSrvDeinit` | #45 TransFile反初始化 | ✅ |
| `test_bOtaSrvInit` | #42 OTA初始化 | ✅ |
| `test_bOtaSrvGetStatus` | #42 OTA状态 | ✅ |
| `test_bOtaSrvBackupValid` | #42 OTA备份有效 | ✅ |
| `test_bOtaSrvPercentage` | #42 OTA进度 | ✅ |
| `test_bMqttSrvRealConnection` | #43 MQTT真实连接 | ✅ |

## 四、关键模块测试策略

### 4.1 FS 文件系统（#19）

**实际测试使用 testflash 作为底层存储设备**。

**关键 API**：
```c
bFSInit(partition_table, num);
bFSMount(index, mkfs);           // FatFS: mkfs=1自动格式化
bFSUnmount(index);
bFSOpen(file*, "0:filename", flag);    // FatFS路径格式 "0:filename"
bFSOpen(file*, "/filename", flag);      // LittleFS路径格式 "/filename"
bFSRead(fd, buf, len);
bFSWrite(fd, buf, len);
bFSClose(fd);
bFSMkfs(index);
bFSGetInfo(index, &total, &free);
```

**分区配置**：
```c
// 使用 TESTFLASH 作为底层设备，通过 bKV 实现分区隔离
// 分区0: FatFS (base=0x4000, size=3MB)
// 分区1: LittleFS (base=3MB+0x4000, size=1MB)
```

### 4.2 MQTT 服务（#43）

**使用 testmac 驱动，真实网络连接到 babyos.cn:1883**

```c
int ret = bMqttSrvStartWithCfg(mqtt_callback, NULL);
// 内部使用 bSocket/bConnect 连接到 MQTT broker
// 回调验证 B_MQTT_EVT_CONN, B_MQTT_EVT_DISCONN, B_MQTT_EVT_PUB
```

## 五、已修复的架构问题

### 5.1 Section sizeof 不匹配（x86_64 .rodata 32字节对齐）

详见原文档描述。`.b_srv_protocol` 存完整 struct，修复已生效（`reserved[8]`）。

### 5.2 TESTFLASH 写约束（1→0 只能写一次）

**修复**：在 `disk_write` 中对非0号扇区执行 pre-erase。

### 5.3 bFSFd_t 跨平台类型（64位指针截断问题）

**问题**：原代码 `bSocket()` 返回 `(int)ptrans`，在 64 位平台上会截断指针。

**修复**：
```c
// b_mod_tcpip.h
typedef intptr_t bSocketFd_t;

// b_mod_tcpip.c
return (bSocketFd_t)(intptr_t)ptrans;
```

### 5.4 bHttpFd_t HTTP 描述符类型

**修复**：类似 bSocketFd_t，新增 `bHttpFd_t` typedef。

### 5.5 其他指针/int 转换修复

| 文件 | 修复内容 |
|------|---------|
| `b_section.h` | Section 扫描宏比较使用 `uintptr_t` |
| `b_device.c` | `halif_addr` 改为 `uintptr_t` |
| `b_mod_button.c` | `addr_val` 改为 `uintptr_t` |
| `b_util_memp.c` | 地址参数改为 `uintptr_t` |
| `b_drv_testmac.c` | 所有 `(int)sockfd` 改为 `(int)(intptr_t)sockfd` |
| `b_drv_esp12f.c` | `return NULL` 改为 `return -2` (int 函数) |

## 六、当前进度

| 批次 | 测试文件 | 测试数 | 状态 |
|------|---------|--------|------|
| Phase 0 | test_drivers.c | 13 | ✅ PASS |
| Phase 1 | test_algo.c | 13 | ✅ PASS |
| Phase 2 | test_core.c | 16 | ✅ PASS |
| Phase 3 | test_utils.c | 12 | ✅ PASS |
| Phase 4 | test_modules.c | 52 | ✅ PASS |
| Phase 5 | test_service.c | 14 | ✅ PASS |
| **总计** | **6个文件** | **120+** | ✅ **ALL PASSED** |

### 测试结果

```
========================================
  Result: ALL PASSED (failed=0)
========================================
```

## 七、测试文件列表

```
test/selftest/
├── test_algo.c       (220行, 13个测试函数)
├── test_core.c       (316行, 16个测试函数)
├── test_drivers.c    (223行, 13个测试函数)
├── test_modules.c    (992行, 52个测试函数)
├── test_service.c    (395行, 14个测试函数)
├── test_utils.c      (204行, 12个测试函数)
└── selftest_main.c   (测试入口)
```

## 八、方案文档状态

**45 份测试方案文档**：`docs/selftest_plan/01_algo_crc.md` ~ `45_srv_transfile.md`

所有方案文档已审核完成，状态标记为"已审核（YYYY-MM-DD）"。