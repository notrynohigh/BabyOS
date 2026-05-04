# 模块 01：CRC 算法

**文件**: `bos/algorithm/algo_crc.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

CRC 模块提供多种 CRC 校验算法，全部为纯软件实现，无任何硬件依赖。分为 CRC8、CRC16、CRC32 三大家族，支持分块计算（chunked）。

### 1.2 算法变体

#### CRC8（4 种）
| 名称 | 多项式 | 模型 | 应用场景 |
|------|--------|------|---------|
| `bCrc8` | 0x07 | ITU | ATM HEC |
| `bCrc8_ITU` | 0x07 | ITU | 同上（别名） |
| `bCrc8_ROHC` | 0x07 | ROHC | IPHC压缩 |
| `bCrc8_MAXIM` | 0x31 | MAXIM | Dallas iButton |

#### CRC16（8 种）
| 名称 | 多项式 | 模型 | 应用场景 |
|------|--------|------|---------|
| `bCrc16_IBM` | 0x8005 | IBM | MODBUS ASCII |
| `bCrc16_MAXIM` | 0x8005 | MAXIM | MODBUS RTU |
| `bCrc16_USB` | 0x8005 | USB | USB PID |
| `bCrc16_MODBUS` | 0x8005 | MODBUS | MODBUS RTU |
| `bCrc16_CCITT` | 0x1021 | CCITT | X.25/PPP |
| `bCrc16_CCITT_FALSE` | 0x1021 | CCITT_FALSE | 虚检率更低 |
| `bCrc16_X25` | 0x1021 | X25 | 信贷校验 |
| `bCrc16_XMODEM` | 0x1021 | XMODEM | 误码检测 |
| `bCrc16_DNP` | 0x3D65 | DNP | 电力协议 |

#### CRC32（2 种）
| 名称 | 多项式 | 模型 | 应用场景 |
|------|--------|------|---------|
| `bCrc32` | 0x04C11DB7 | PKZIP/ETHERNET | 压缩校验 |
| `bCrc32_MPEG2` | 0x04C11DB7 | MPEG2 | 视频流 |

### 1.3 核心 API

```c
// 单次计算（适用于小数据）
uint32_t crc_calculate(uint8_t type, uint8_t *pbuf, uint32_t len);

// 分块计算（适用于大数据流）
void crc_calculate_sbs(algo_crc_sbs_t *phandle, uint8_t *pbuf, uint32_t len);

// 句柄宏
CRC_REG_SBS_HANDLE(name, crc_type);   // 创建句柄
CRC_SBS_HANDLE_RESET(name, crc_type); // 重置句柄
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bCrc8` | CRC8 标准向量 "123456789" | 返回 0xF4 |
| `test_bCrc16` | CRC16-CCITT-FALSE / MODBUS / X25 | 0x29B1 / 0x4B37 / 0x906E |
| `test_bCrc32` | CRC32 标准向量 "123456789" | 返回 0xCBF43926 |
| `test_bCrcChunked` | 分块计算与单次计算结果一致 | Smoke test |

**测试向量**: "123456789" (0x31 0x32 0x33 0x34 0x35 0x36 0x37 0x38 0x39)

---

## 三、测试文件

```
test/selftest/test_algo.c
```

**依赖**:
- `bos/algorithm/algo_crc.c`
- `bos/algorithm/inc/b_algo_crc.h`
- 无任何硬件依赖

---

## 四、备注

CRC 模块是 BabyOS 中最纯粹的算法模块，测试通过标准向量验证正确性。