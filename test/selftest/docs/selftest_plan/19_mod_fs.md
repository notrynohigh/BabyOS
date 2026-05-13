# 模块 19：FS 文件系统

**文件**: `bos/modules/b_mod_fs.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

FS 模块封装 FatFS 和 LittleFS，提供统一文件系统 API。

### 1.2 核心 API

```c
// 初始化
int bFSInit(const bFSPartition_t *partition, uint8_t partition_number);

// 挂载/卸载
int bFSMount(uint8_t index, uint8_t mkfs);  // BFS_MKFS_FATFS/BFS_MKFS_LITTLEFS
int bFSUnmount(uint8_t index);

// 文件操作
int bFSOpen(bFSFile_t *fil, const char *path, int flag);
int bFSRead(int fd, uint8_t *pbuf, uint32_t len);
int bFSWrite(int fd, uint8_t *pbuf, uint32_t len);
int bFSClose(int fd);
int bFSGetInfo(uint8_t index, uint32_t *ptotal_size, uint32_t *pfree_size);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bFSInit` | 正常初始化 | 返回 0 |
| `test_bFSInitNull` | partition=NULL | 返回 -1 |
| `test_bFSMountFatfs` | 挂载 FatFS 分区 | 返回 0 |
| `test_bFSMountLittlefs` | 挂载 LittleFS 分区 | 返回 0 |
| `test_bFSUnmountFatfs` | 卸载 FatFS | 返回 0 |
| `test_bFSUnmountLittlefs` | 卸载 LittleFS | 返回 0 |
| `test_bFSOpenWriteReadFatfs` | FatFS 分区写读 | 数据一致 |
| `test_bFSOpenWriteReadLittlefs` | LittleFS 分区写读 | 数据一致 |
| `test_bFSGetInfoFatfs` | 查询 FatFS 空间 | total>0, free<=total |
| `test_bFSGetInfoLittlefs` | 查询 LittleFS 空间 | total>0, free<=total |
| `test_bFSCoexistBothFS` | 两分区同时操作 | 均正常 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_fs.c`
- `bos/modules/inc/b_mod_fs.h`
- FatFS / LittleFS 库
- TESTFLASH

---

## 四、备注

FatFS 使用 "0:" 前缀路径，LittleFS 使用 "/" 前缀。