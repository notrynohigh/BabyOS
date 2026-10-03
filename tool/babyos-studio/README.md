# BabyOS Studio

BabyOS 统一桌面调试工具（Electron + Python FastAPI）。面向 BabyOS 固件的真实上位机能力：串口 b_protocol 协议栈、OTA / 文件传输、Xmodem/Ymodem、Shell 参数调节、HTTP Mock，以及 AutoML「训练 → 导出深度绑定 C bundle」。

**本 README 描述的是仓库中已实现的真实功能。** 协议帧格式与命令以 master 分支 `tool/README.md` + `tool/b_protocol.py` + `bos/modules/b_mod_protocol.*` 为准；无串口硬件时，协议/传输可用 Mock 设备或 PTY 做主机侧验证（见 §11）。

---

## 1. 简介与架构

### 1.1 能力一览

| 能力 | UI 页面 | 后端 API 前缀 | 实现 |
|---|---|---|---|
| 串口连接 / 协议测试 / 设时间 | 串口控制 | `/api/device/serial/*` `/api/device/protocol/*` | `python/device/uart_service.py` + `protocol_client.py` |
| OTA 固件升级（0x3/0x4/0x5） | OTA 升级 | `/api/device/ota/*` | `ProtocolClient.start_ota` |
| 任意文件写 FLASH（0x6 + 0x4/0x5） | （API） | `/api/device/file/*` | `ProtocolClient.start_file_transfer` |
| Xmodem-128 / Ymodem-1K | Xmodem/Ymodem | `/api/device/xmodem/*` `/api/device/ymodem/*` | `python/device/xmodem_ydmodem.py` |
| UID / SN / 设备信息（0x7/0x8/0xA） | 设备信息 | `/api/device/uid|sn|info` | `protocol_client.py` + `sn_util.py` |
| Shell 参数调节（非协议） | 参数调节 | `/api/device/param/*` `/api/device/shell/cmd` | `python/device/shell_client.py` |
| HTTP Mock + 主机代发 | HTTP 调试 | `/api/device/http/*` | `python/device/http_mock.py` |
| AutoML 全流程 | 工程向导 | `/api/projects/*` `/api/templates/*` | FastAPI AutoML 服务 + 导出生成器 |

### 1.2 进程与数据流

```
Electron UI (ui/app.js)
    │  HTTP 127.0.0.1:18080
    ▼
FastAPI (python/app/main.py)
    │
    ├─ DeviceManager (python/device/device_manager.py)  ← 进程级单例
    │     ├─ UartService      pyserial 真实串口
    │     ├─ ProtocolClient   b_protocol 帧 + OTA/文件状态机
    │     ├─ ShellClient      文本 shell（param ...）
    │     ├─ HttpMock         本机 ThreadingHTTPServer
    │     └─ XmodemSender / YmodemSender
    │
    └─ AutoML 工程服务 (projects/datasets/labels/features/training/export)
          └─ 导出 bundle → bos/algorithm 深度绑定 C 代码
```

要点：

- **串口句柄唯一**：UI 打开串口优先走 Python `DeviceManager`；协议、OTA、Shell、Xmodem **共用同一 UART**。Electron 原生 serial 仅作回退（原始收发，**不支持 b_protocol**）。
- **协议命令与 Shell 分离**：0x1–0xA 走 b_protocol 帧；参数调节走 `nr_micro_shell` 文本命令 `param ...`。
- **HTTP 设备侧触发**：设备 HTTP 由固件 `bHttp` / 工程代码发起，**不是** b_protocol 命令。Studio 提供真实 Mock 服务器 + 主机侧 `/api/device/http/proxy` 代发，用于联调 Mock；固件请求 Mock 地址后，`GET /api/device/http/requests` 可查看流量。

### 1.3 关键源码路径

| 路径 | 作用 |
|---|---|
| `python/device/b_protocol.py` | 帧 pack/parse、TEA、命令常量、负载构造 |
| `python/device/protocol_client.py` | 请求响应、OTA/文件传输泵 |
| `python/device/xmodem_ydmodem.py` | Xmodem-128 / Ymodem-1K 状态机 |
| `python/device/shell_client.py` | `param` 列表/读/写（写后回读校验） |
| `python/device/http_mock.py` | 真实 HTTP(S) Mock + 请求日志 |
| `python/app/api/device.py` | 设备相关 FastAPI 路由 |
| `python/app/services/export/generator.py` | bundle 组装（文件名 / Makefile / b_config） |
| `python/app/services/export/feature_cgen.py` | 特征 C 生成（绑定 `bAlgoSignal*` / `bAlgoFft*`） |
| `python/app/services/export/model_cgen.py` | 模型 C 生成（绑定 `bAlgoMl*`） |
| `bos/algorithm/inc/algo_ml.h` `algo_signal.h` `algo_fft.h` | 固件算法原语契约 |
| `test/device_features/` | 设备功能主机侧测试 |

---

## 2. 安装与启动

### 2.1 依赖

- Node.js ≥ 18（Electron 28）
- Python 3.8+（AutoML / 设备后端；代码按 **Python 3.8** 兼容编写）
- 串口权限：Linux 需将用户加入 `dialout`（或等价组）

### 2.2 开发模式启动

```bash
cd tool/babyos-studio
./start_dev.sh          # Linux / macOS
start_dev.bat           # Windows
```

脚本会：检查/安装 Node 与 Python → 创建 venv 并安装 `python/requirements.txt` → `npm install`（失败时尝试 npmmirror）→ `npm run dev` 启动 Electron。

后端默认监听 **`http://127.0.0.1:18080`**（CORS 仅放行 `127.0.0.1` / `localhost`）。

也可手动：

```bash
cd tool/babyos-studio
python3 -m venv python/.venv
source python/.venv/bin/activate
pip install -r python/requirements.txt
python -m uvicorn app.main:app --app-dir python --host 127.0.0.1 --port 18080 &
npm install
npm run dev
```

### 2.3 构建安装包

```bash
cd tool/babyos-studio
npm install
npm run build          # 全平台
npm run build:win      # NSIS .exe
npm run build:mac      # DMG
npm run build:linux    # AppImage
```

### 2.4 健康检查

| 检查 | 方式 |
|---|---|
| 后端版本 | `GET http://127.0.0.1:18080/api/version` → `{"version":"1.0.0"}` |
| 设备聚合状态 | `GET /api/device/status` |
| 串口枚举 | `GET /api/device/serial/ports` |
| UI 状态栏 | 首页「Python 状态 / 串口状态」；未就绪会自动重试约 30s |

---

## 3. BabyOS 协议摘要

> 完整命令表见 master 分支 `tool/README.md`；实现见 `python/device/b_protocol.py`、固件 `bos/modules/b_mod_protocol.*`。

### 3.1 帧格式

```
HEAD(0xFE) + DeviceID(4B LE) + Length(2B LE) + CMD(1B) + Params(nB) + Checksum(1B)
```

| 字段 | 说明 |
|---|---|
| HEAD | 固定 `0xFE` |
| DeviceID | 上位机 ID **`0x1314`**；`0xFFFFFFFF`（INVALID_ID）表示「任意 id」 |
| Length | `1 + param_len`（含 CMD 字节） |
| Checksum | 除校验字节外全部字节之和，`mod 256` |
| TEA（可选） | 整帧按 8 字节块加密；尾部不足 8 字节不加密 |

TEA：16 轮，key = `(1, 22, 333, 4444)`，delta = `0x9E3779B9`。打开串口时勾选「加密传输」→ `ProtocolClient(encrypt=True)`。

固件侧接受条件（主机侧约定）：`id == host_id || id == INVALID_ID || device_id == INVALID_ID`。主机→设备帧常用 `INVALID_ID`。

### 3.2 命令表（Studio 主机客户端实现 0x1–0xA）

| CMD | 名称 | 方向 | 参数 |
|---|---|---|---|
| 0x1 | TEST | H→D | `"BabyOS"`（7B） |
| 0x2 | UTC | H→D | UTC 秒，4B LE |
| 0x3 | FW_INFO | H→D | `size(4) + crc32(4) + filename[64]`（不足补 0） |
| 0x3 | FW_INFO ACK | D→H | 无参数 |
| 0x4 | FDATA 请求 | D→H | 分包序号 2B LE（从 0 起） |
| 0x4 | FDATA 数据 | H→D | `seq(2) + data[512]`（不足补 0） |
| 0x5 | OTA/传输结果 | D→H | 1B：`0`成功 `1`CRC 错 `2`名不匹配 `3`长度不合理 `4`超时 |
| 0x5 | 结果 ACK | H→D | 无参数 |
| 0x6 | TRANS_FILE | H→D | `size(4)+crc32(4)+dev_no(4)+offset(4)`，数据仍走 0x4 |
| 0x7 | GET_UID | H→D / D→H | 回复：`uid_len(1) + uid[n]` |
| 0x8 | WRITE_SN | H→D / D→H | 参数：`sn_len(1) + sn[n]`；回复无参数 |
| 0x9 | TSL 调用 | H→D / D→H | 方法调用内容（Studio 主机侧保留常量，无独立 UI） |
| 0xA | DEVICEINFO | H→D / D→H | 回复：`version[16] + name/model[16]` |

文档中另有网络/语音指令（0x30/0x31、0x40–0x44）；**当前 Studio 主机客户端聚焦 0x1–0xA**。

### 3.3 CRC

| 算法 | 口径 | 校验值 |
|---|---|---|
| CRC32（OTA / 0x6 文件） | 反射多项式 `0xEDB88320`，init `0xFFFFFFFF`，xorout `0xFFFFFFFF`；空缓冲 → 0 | `"123456789"` → `0xCBF43926`（同 `zlib.crc32`） |
| Ymodem/Xmodem 传输 CRC16 | `crc = (crc ^ byte) << 8`，再 8 次 `<<1`（MSB 时 XOR `0x1021`）；**不是** 标准 CRC-16/XMODEM | 空 → `0x0000`；`"123456789"` → `0x2672`；`"BabyOS"` → `0x5424` |
| 标准 CRC-16/XMODEM | `crc ^= byte << 8`，多项式 `0x1021`（固件 `algo_crc.c` 另有 ALGO_CRC16_XMODEM） | `"123456789"` → `0x31C3` |

实现：`python/device/crc_util.py`（CRC32 + 标准 XMODEM）、`python/device/xmodem_ydmodem.py`（Ymodem 专用 CRC16）。

### 3.4 SN 生成（0x8）

与 master `mainwindow.py` 一致：

```text
sn_body = bytes([16]) + bytes(md5(uid)[i] | orval  for i in range(16))
```

- `md5(uid)` 取 **原始 16 字节**，不是 hex 字符串。
- `sn_body` **已含长度前缀**，直接作为 `pack_frame(..., CMD_WRITE_SN, sn_body)` 的 param；不要再包一层长度。

---

## 4. 串口控制

### 4.1 UI 操作（串口控制页）

1. **串口** 下拉框（`GET /api/device/serial/ports` 枚举；失败时回退 Electron serialport）。
2. **波特率**（UI 默认 115200）。
3. 可选 **加密传输**（TEA）。
4. **打开串口** → `POST /api/device/serial/open` `{path, baud, encrypt}`。
5. **发送测试指令** → `POST /api/device/protocol/test`（CMD 0x1，param=`BabyOS`）。
6. **设置时间** → `POST /api/device/protocol/set_time` `{utc}`（缺省用主机当前 UTC）。
7. **关闭串口** → `POST /api/device/serial/close`。

日志区会打印 TX/RX 与协议结果；首页「串口状态」同步为 `已连接: <port> @ <baud>`。

### 4.2 API

| 方法 | 路径 | 请求 | 成功响应（节选） |
|---|---|---|---|
| GET | `/api/device/serial/ports` | — | `{ports:[...], open:bool, current:str}` |
| POST | `/api/device/serial/open` | `{path, baud=115200, encrypt=false}` | `{ok, port, baudrate, encrypt}` |
| POST | `/api/device/serial/close` | `{}` | `{ok}` |
| POST | `/api/device/protocol/test` | `{}` | `{cmd, param_text, param_hex}` |
| POST | `/api/device/protocol/set_time` | `{utc?}` | `{cmd, utc}` |

错误码（结构化 `detail.code`）：

| code | HTTP | 含义 |
|---|---|---|
| `SERIAL_NOT_OPEN` | 409 | 需先打开串口 |
| `PROTOCOL_TIMEOUT` | 504 | 超时无设备响应 |
| `INVALID_REQUEST` | 400 | 非法 baud/path 等 |

### 4.3 curl 示例

```bash
# 枚举串口
curl -s http://127.0.0.1:18080/api/device/serial/ports

# 打开（Linux 常见 /dev/ttyUSB0 或 /dev/ttyACM0）
curl -s -X POST http://127.0.0.1:18080/api/device/serial/open \
  -H 'Content-Type: application/json' \
  -d '{"path":"/dev/ttyUSB0","baud":115200,"encrypt":false}'

# 协议测试：期望 param_text == "BabyOS"
curl -s -X POST http://127.0.0.1:18080/api/device/protocol/test

# 设置 UTC
curl -s -X POST http://127.0.0.1:18080/api/device/protocol/set_time \
  -H 'Content-Type: application/json' -d '{"utc":1700000000}'

# 关闭
curl -s -X POST http://127.0.0.1:18080/api/device/serial/close
```

> Electron 原生串口回退仅用于原始收发调试；**协议/OTA/Shell/Xmodem 在 Python 持有串口时才可用**。

---

## 5. OTA 升级（CMD 0x3 / 0x4 / 0x5）

### 5.1 固件侧流程

```
Host                         Device
  |-- 0x3 FW_INFO(size,crc32,filename[64]) -->
  |<-- 0x3 ACK ------------------------------
  |<-- 0x4 FDATA 请求(seq=0) ----------------
  |-- 0x4 FDATA(seq=0, data[512]) ----------->
  |<-- 0x4 FDATA 请求(seq=1) ----------------
  |-- 0x4 ...  (直到文件发完，尾包补 0) ----->
  |<-- 0x5 result (0..4) --------------------
  |-- 0x5 ACK ------------------------------->
```

### 5.2 UI 操作（OTA 页）

1. 打开串口。
2. **选择文件** → 固件绝对路径（`electronAPI.dialog.openFile`）。
3. **固件名称**（可选；默认取文件名）。设备侧应与期望固件名匹配，否则 result=`2`。
4. **开始 OTA 升级** → `POST /api/device/ota/start`。
5. UI 以约 400ms 轮询 `GET /api/device/ota/status?job_id=...`，进度条更新。

### 5.3 API

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/device/ota/start` | `{path, name?, timeout=30.0}` → `{accepted, job_id, status, status_url}` |
| GET | `/api/device/ota/status?job_id=` | `{job:{state,progress,ok,result_code,error,...}, kind, transfer_active, transfer_result, uart_open}` |

`job.state`：`starting|running|done|error|cancelled`。`result_code` 对应 §3.2 的 0–4。
`/api/device/file/status` 与 OTA 同构（共享 job 注册表，`kind="file"`）。

```bash
curl -s -X POST http://127.0.0.1:18080/api/device/ota/start \
  -H 'Content-Type: application/json' \
  -d '{"path":"/path/to/app.bin","name":"app.bin","timeout":60}'

# 轮询
curl -s "http://127.0.0.1:18080/api/device/ota/status?job_id=<job_id>"
```

实现：`ProtocolClient.start_ota` → `python/device/protocol_client.py`；CRC32 使用 §3.3 固件口径。

---

## 6. 文件传输（CMD 0x6）与 Xmodem / Ymodem

### 6.1 CMD 0x6 — 任意文件写 FLASH

与 OTA 同数据泵（0x4/0x5），**首帧不同**：

| 方向 | CMD | 参数 |
|---|---|---|
| H→D | 0x6 | `size(4) + crc32(4) + dev_no(4) + offset(4)` |
| D→H | 0x4 | 请求分包序号 |
| H→D | 0x4 | `seq(2)+data[512]` |
| D→H | 0x5 | 结果码 |

**API 已实现**（UI 无独立 0x6 页面，可直接调 API）：

```bash
POST /api/device/file/start   {"path": "...", "dev_no": 0, "offset": 0, "timeout": 30.0}
GET  /api/device/file/status?job_id=...
POST /api/device/file/stop
```

```bash
curl -s -X POST http://127.0.0.1:18080/api/device/file/start \
  -H 'Content-Type: application/json' \
  -d '{"path":"/path/to/config.bin","dev_no":0,"offset":0,"timeout":30}'
```

### 6.2 Xmodem-128

| 项 | 值 |
|---|---|
| 块大小 | 128 字节 |
| 帧 | `SOH + blk + ~blk + 128B + CRC16(2B BE)` = 133B |
| CRC | §3.3 BabyOS Ymodem/Xmodem-transfer CRC16（**不是**标准 0x31C3） |
| 控制 | SOH/STX/EOT/ACK/NAK/CAN；CRC 模式由接收端 `C` 启动 |

UI：Xmodem/Ymodem 页 → 选择文件 → **开始发送** → 轮询 `GET /api/device/xmodem/status?kind=xmodem&job_id=`。

```bash
curl -s -X POST http://127.0.0.1:18080/api/device/xmodem/start \
  -H 'Content-Type: application/json' -d '{"path":"/path/to/file.bin"}'
curl -s -X POST http://127.0.0.1:18080/api/device/xmodem/cancel
curl -s "http://127.0.0.1:18080/api/device/xmodem/status?kind=xmodem"
```

### 6.3 Ymodem-1K

| 项 | 值 |
|---|---|
| Block 0 | `SOH + 00 + FF + "name\0size\0" + pad(128) + CRC16` |
| 数据块 | `STX + blk + ~blk + 1024B + CRC16` = 1029B |
| 尾包 | `SOH + blk + ~blk + zeros(128) + CRC16` |
| 文件名 | `DeviceManager.ymodem_filename`（默认取路径 basename） |

```bash
curl -s -X POST http://127.0.0.1:18080/api/device/ymodem/start \
  -H 'Content-Type: application/json' -d '{"path":"/path/to/file.bin"}'
curl -s -X POST http://127.0.0.1:18080/api/device/ymodem/cancel
curl -s "http://127.0.0.1:18080/api/device/xmodem/status?kind=ymodem"
```

同一时刻仅允许一个活动传输；关串口会先取消 Xmodem/Ymodem 与协议传输。

---

## 7. 设备信息：UID / SN / DEVINFO

UI「设备信息」页（均需 Python 持有串口）：

| 按钮 | API | 协议 | 说明 |
|---|---|---|---|
| 获取 UID | `POST /api/device/uid/get` | 0x7 | 响应 `{uid_hex, uid_len}`；缓存于 DeviceManager |
| 写入 SN | `POST /api/device/sn/write` `{orval, uid_hex?}` | 0x8 | 缺省用缓存 UID；可传 `uid_hex` 覆盖 |
| 获取设备信息 | `POST /api/device/info/get` | 0xA | `{version, model}`（各 16 字节，去尾零） |

SN 算法见 §3.4。示例：

```bash
curl -s -X POST http://127.0.0.1:18080/api/device/uid/get
# {"uid_hex":"...", "uid_len":12}

curl -s -X POST http://127.0.0.1:18080/api/device/sn/write \
  -H 'Content-Type: application/json' -d '{"orval":0}'
# {"sn_hex":"...", "orval":0, ...}

curl -s -X POST http://127.0.0.1:18080/api/device/info/get
# {"version":"...","model":"..."}
```

错误：`PROTOCOL_TIMEOUT` 504；SN 在无 UID 且未传 `uid_hex` 时结构化报错。

---

## 8. 参数调节（Shell）

**不是 b_protocol 命令**，是固件 `nr_micro_shell` 文本协议（行协议，`\r` 结束）：

| 发送 | 期望回显 | 含义 |
|---|---|---|
| `param` | 每行 `: name` | 列出全部参数名 |
| `param name` | `name:value` | 读取 |
| `param name value` | （通常静默） | 设置；客户端用 **回读校验** |

### 8.1 UI（参数调节页）

- **列出全部** → `POST /api/device/param/list`
- **读取参数** → `POST /api/device/param/get` `{name}`
- **设置参数** → `POST /api/device/param/set` `{name, value, verify=true}`  
  纯整数字符串会按 `int` 发送；`verified=true` 表示回读成功。
- **自定义命令** → `POST /api/device/shell/cmd` `{cmd, timeout=1.0}`（原样发送，返回 `response` 文本）

### 8.2 API

```bash
curl -s -X POST http://127.0.0.1:18080/api/device/param/list
# {"count":3,"names":["baud","interval","threshold"]}

curl -s -X POST http://127.0.0.1:18080/api/device/param/get \
  -H 'Content-Type: application/json' -d '{"name":"interval"}'
# {"name":"interval","value":100}

curl -s -X POST http://127.0.0.1:18080/api/device/param/set \
  -H 'Content-Type: application/json' -d '{"name":"interval","value":200,"verify":true}'

curl -s -X POST http://127.0.0.1:18080/api/device/shell/cmd \
  -H 'Content-Type: application/json' -d '{"cmd":"help","timeout":1.0}'
```

错误：`PARAM_NOT_FOUND` 504/404 类；`PARAM_SET_FAILED` 500（校验不通过或超时）。

---

## 9. HTTP Mock

### 9.1 能力

- 真实 `ThreadingHTTPServer`，绑定 `127.0.0.1`，支持 HTTP/HTTPS。
- 记录每条请求：`method / path / query / body / body_hex / headers / client`。
- 可配置默认响应：`body / content_type / status_code`；可选 per-path 覆盖（服务层 `set_path_response`）。
- Mock 自带 `GET /_requests` → JSON 日志。
- HTTPS：进程内 `openssl` 生成一次自签名证书（`CN=127.0.0.1` + SAN）。

### 9.2 UI（HTTP 调试页）

1. **启动服务器** → `POST /api/device/http/start`  
   `{port=0, body, content_type, status_code, https=false}`  
   `port=0` 表示随机端口，状态显示 `base_url`。
2. **发送请求** → **主机代发** `POST /api/device/http/proxy` `{url, method, body?, headers?, timeout, verify_tls}`。  
   这是本机真实 HTTP 请求，用于联调 Mock；**设备侧 HTTP 仍由固件发起**，请让固件请求 `http://127.0.0.1:<mock_port>/...`（或实验室可达地址）。
3. **请求日志** 显示代理结果 + Mock 记录条数。
4. **停止服务器** → `POST /api/device/http/stop`。

> UI 中的「初始化客户端 / 反初始化」按钮无对应后端端点，**不构成已实现功能**；请用 Mock + 设备固件 HTTP 客户端，或用 proxy 验证 Mock 配置。

### 9.3 API

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/device/http/start` | 启动 Mock；已运行则 409 `HTTP_MOCK_RUNNING` |
| POST | `/api/device/http/stop` | 停止 |
| GET | `/api/device/http/status` | `{running, port, base_url, https, status_code, request_count}` |
| GET | `/api/device/http/requests` | `{count, requests:[...]}` |
| POST | `/api/device/http/proxy` | 主机侧真实请求；非 HTTP method → 400 |

```bash
# 启动 Mock
curl -s -X POST http://127.0.0.1:18080/api/device/http/start \
  -H 'Content-Type: application/json' \
  -d '{"port":18081,"body":"{\"ok\":true}","content_type":"application/json","status_code":200,"https":false}'

# 主机代发（打到 Mock）
curl -s -X POST http://127.0.0.1:18080/api/device/http/proxy \
  -H 'Content-Type: application/json' \
  -d '{"url":"http://127.0.0.1:18081/api/status","method":"GET","timeout":5.0}'

# 查看 Mock 流量
curl -s http://127.0.0.1:18080/api/device/http/requests
# 或直接
curl -s http://127.0.0.1:18081/_requests
```

---

## 10. AutoML 完整手册

AutoML 路径：工程 → 数据 → 标注 → 特征 → 训练 → 导出 C bundle → 固件集成（深度绑定 `bos/algorithm`）。

### 10.1 创建工程（UI）

1. 工程页 → **+ 新建项目**。
2. 填写：
   - **名称**（1–64 字符）
   - **模式**：`timeseries`（时序）| `table`（表格分类/回归）
   - **任务类型**：`classification` | `regression`
   - **采样率**（时序，>0 Hz；影响窗口秒数与频域特征）
3. **确认创建** → `POST /api/projects`。

对应 API：`POST /api/projects` body=`{name, mode, task_type, sampling_rate}`。

### 10.2 数据导入（CSV / 模板）

**模板**

- UI：**下载示例CSV** → 前端生成 `sample_timeseries.csv`  
  列：`timestamp,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z`（11 行示例）。
- 后端模板包：`GET /api/templates/{kind}`，`kind ∈ {timeseries, table}` → zip。

**导入**

1. 打开工程 → 数据页 → **导入数据**。
2. 选择 `csv` 或 `npz`（可多文件）。
3. UI 预览后发送：`POST /api/projects/{pid}/dataset`  
   multipart：`files`（复数）、`mapping`（JSON 字符串）、`import_kind=append|replace`。

**mapping 字段（真实校验）**

| mode | 必填 | 说明 |
|---|---|---|
| `table` | `label_col` | 标签列名 |
| `table` | `features` 或 `channels` | 至少 1 个特征列 |
| `timeseries` | `channels` | 至少 1 个通道列 |
| 可选 | `ts_col` | 时间戳列；可从数据估计 `sampling_rate` |
| 可选 | `label_col` | 时序模式也可用标签列（分段来源之一） |

其他数据 API：

| 方法 | 路径 | 作用 |
|---|---|---|
| POST | `/api/projects/{pid}/dataset/preview` | 单文件预览 |
| GET | `/api/projects/{pid}/dataset` | 文件列表与统计 |
| GET | `/api/projects/{pid}/dataset/file/{fid}/data?limit=&offset=` | 分页数据 |
| DELETE | `/api/projects/{pid}/dataset/file/{fid}` | 删文件 |

训练进行中导入/删文件 → 409 `TRAINING_ACTIVE`。

### 10.3 标注与分段（时序）

| 操作 | API |
|---|---|
| 添加标注 | `POST /api/projects/{pid}/labels` `{name, color?}` |
| 重命名 | `PATCH /api/projects/{pid}/labels/{label_id}` |
| 删除标注 | `DELETE /api/projects/{pid}/labels/{label_id}` |
| 添加分段 | `POST /api/projects/{pid}/segments` `{file_id, start, end, label_id}`（`end > start`） |
| 波形选区 | UI 图表选区 → 自动填 start/end 后添加 |

表格模式：数据导入时 `label_col` 即标签，无需分段。

### 10.4 特征工程

UI「特征」页 → 配置 → **计算特征**。

**API**

| 方法 | 路径 | 作用 |
|---|---|---|
| GET | `/api/projects/{pid}/features/config` | 读取配置 |
| PUT | `/api/projects/{pid}/features/config` | 写入配置（训练中 409） |
| GET | `/api/projects/{pid}/features/categories` | 分类 + 通道 + 全特征名 |
| POST | `/api/projects/{pid}/features/compute` | 构建/复用特征矩阵 |
| GET | `/api/projects/{pid}/features/matrix` | 矩阵元数据 |
| GET | `/api/projects/{pid}/features/scoring?method=` | `f_test\|mutual_info\|variance` |

**FeatureConfig 字段**

| 字段 | 时序工程实际默认（GET config） | schema 字段默认 | 说明 |
|---|---|---|---|
| `window_len_s` | 2.0 | 2.0 | 时序窗口秒数 |
| `n_per_window` | 512 | 512 | 窗口点数（有效长度会偶数对齐） |
| `step` | **64** | 1 | 滑窗步进；**不得 < eff_n/10**（512 点时最小 51）。schema 默认 1 仅作占位，时序工程未保存配置时 `GET config` 返回 `feature_service.DEFAULT_CONFIG.step=64` |
| `feature_ids` | `["mean","std","rms","ptp","zcr"]` | `[]` | 表格模式可空=全部数值列；时序 ≥1。时序未保存配置时返回 DEFAULT 那 5 个；表格模式返回空 |
| `channel_features` | 按通道复制上述 5 特征 | null | 逐通道特征映射，如 `{"accel_x":["mean","std"]}` |
| `freq_enabled` | false | false | 启用频域特征 |
| `freq_bands` | 5 | 5 | `band_ratio` 展开为 `band0_ratio..band{k-1}_ratio` |
| `norm` | `zscore` | `zscore` | `none\|minmax\|zscore\|robust`（训练矩阵归一；导出 predict 内部另烘焙标准差） |

**时域特征（TIME_FEATURES）**

`mean, std, variance, min, max, rms, abs_mean, ptp, zcr, autocorr, skew, kurt`

**频域特征（FREQ_FEATURES）**

`spec_centroid, spec_energy, dominant_freq, band_ratio`（展开为 `band{i}_ratio`）

**分类（UI 分组）**

| key | 标签 | 特征 |
|---|---|---|
| `statistical` | 统计类 | mean, std, variance, min, max, skew, kurt |
| `amplitude` | 幅值类 | rms, abs_mean, ptp |
| `time_domain` | 时域类 | zcr, autocorr |
| `frequency` | 频域类 | spec_centroid, spec_energy, dominant_freq, band_ratio |

> `feature_ids` 必须是 **函数名**（如 `mean`），不是通道名。数值口径：std/moments 为总体口径（除 N）。

### 10.5 训练

UI「训练」页 → **开始训练** / **停止训练**；查看 leaderboard / 最佳模型 / 特征重要性。

**TrainConfig**

| 字段 | 默认 | 约束 |
|---|---|---|
| `k` | 5 | 2–10（CV 折数） |
| `n_iter` | 30 | 1–500（候选数） |
| `budget_s` | 600 | ≥10 |
| `metric` | `f1_macro` | 分类/回归主指标 |
| `task_type` | `classification` | `classification\|regression` |
| `auto_feature_select` | true | 训练时再筛特征 |
| `top_n` | 20 | 2–… |
| `scoring` | `f_test` | `f_test\|mutual_info\|variance` |
| `seed` | 42 | 可复现 |

**模型池（真实采样）**

| 任务 | model_type |
|---|---|
| classification | `dt, rf, et, lr, nb, mlp, xgb, lgbm, simple_nn` |
| regression | `dt_r, rf_r, et_r, lr_r, xgb_r, lgbm_r, simple_nn_r` |

（无 xgboost/lightgbm 时自动回退到 `dt` / `dt_r`。）

**API**

| 方法 | 路径 | 作用 |
|---|---|---|
| POST | `/api/projects/{pid}/training` | 启动（body=TrainConfig） |
| GET | `/api/projects/{pid}/training` | 状态 `idle/running/done/cancelled/interrupted/failed` |
| POST | `/api/projects/{pid}/training/cancel` | 取消 |
| GET | `/api/projects/{pid}/training/leaderboard` | 候选排行 |
| POST | `/api/projects/{pid}/training/set_best` | `{cand_id}` 设为最佳 |
| GET | `/api/projects/{pid}/training/best` | 最佳模型信息 |
| GET | `/api/projects/{pid}/training/feature_importance` | 特征重要性 |
| GET | `/api/projects/{pid}/training/report` | 完整报告 |

进程重启时启动对账：残留 `running` → `interrupted`。

### 10.6 导出 C bundle（真实文件名）

UI「导出」页 → **导出 C 代码** → `POST /api/projects/{pid}/export`。

**bundle 文件（工程名 slug 化后）**

| 文件 | 内容 |
|---|---|
| `algo_<name>.h` / `algo_<name>.c` | 模型 `algo_<name>_predict()`，归一化已烘焙进内部 |
| `algo_<name>_feat.h`（时序） | 仅 `algo_<name>_feat_extract()` 声明/include 指引；**实现与模型同在 `algo_<name>.c` 编译单元**（共享静态 FFT 缓冲） |
| `example/algo_<name>_test.c` | BabyOS 轮询任务示例（`BOS_REG_POLLING_FUNC`） |
| `main.c` | host 自检入口（`AUTOML_SKIP_MAIN` 可跳过） |
| `Makefile.snippet` | 工程 Makefile 集成片段 |
| `b_config_snippet.h` | `_BOS_ALGO_ENABLE` / `_ALGO_ML_ENABLE` 等宏片段 |
| `section.txt` | 链接段约定 |
| `README.md` | bundle 内说明 |
| `export_report.json` | 自检结果（编译/数值/特征链/静态扫描/符号清单） |

仓库样例：`test/automl_e2e/` 中可见 `algo_e2e_test_automl.c/.h`、`algo_e2e_test_automl_feat.h`、`Makefile.snippet`、`b_config_snippet.h`、`section.txt`。

**下载 / 目录**

```bash
POST /api/projects/{pid}/export
GET  /api/projects/{pid}/export
GET  /api/projects/{pid}/export/download/<filename>.zip
GET  /api/projects/{pid}/export/dir
```

### 10.7 固件集成

#### （1）Kconfig / b_config.h

至少启用：

```c
#define _BOS_ALGO_ENABLE   1
#define _ALGO_ML_ENABLE    1
```

时序工程还需：

```c
#define _ALGO_SIGNAL_ENABLE 1   /* 时域特征 */
#define _ALGO_FFT_ENABLE    1   /* 频域特征 */
```

menuconfig 路径：Algorithm Configuration → `_BOS_ALGO_ENABLE` → `_ALGO_ML_ENABLE`（及 signal/fft）。

#### （2）Makefile（真实片段形态）

```make
BUNDLE_SRC := bundle/algo_<name>.c
BUNDLE_SRC += $(wildcard bundle/example/*.c)
BUNDLE_SRC += $(BUNDLE_SRC_DIR)/bos/algorithm/algo_ml.c
# 时序且启用 signal/fft 时追加:
# BUNDLE_SRC += $(BUNDLE_SRC_DIR)/bos/algorithm/algo_signal.c
# BUNDLE_SRC += $(BUNDLE_SRC_DIR)/bos/algorithm/algo_fft.c
CFLAGS  += -D_ALGO_ML_ENABLE=1
CFLAGS  += -D_ALGO_SIGNAL_ENABLE=1
CFLAGS  += -D_ALGO_FFT_ENABLE=1
CFLAGS  += -ffp-contract=off
INCLUDES += -Ibundle
INCLUDES += -I$(BUNDLE_SRC_DIR)/bos/algorithm
INCLUDES += -I$(BUNDLE_SRC_DIR)/bos/algorithm/inc
SRCS += $(BUNDLE_SRC)
```

> `test/automl_e2e/Makefile.snippet` 为真实导出样例（工程名 `e2e_test_automl`）。

#### （3）main.c / 轮询任务

```c
#include "b_os.h"
#include "algo_<name>.h"

int main(void) {
    bInit();
    while (1) {
        bExec();   /* 驱动 BOS_REG_POLLING_FUNC 注册的任务 */
    }
}
```

时序示例（bundle `example/algo_<name>_test.c` 同构）：

```c
#include "b_os.h"
#include "algo_<name>.h"

static float s_ch_buf[N_CH][ALGO_<NAME>_WIN_LEN];
static float s_features[ALGO_<NAME>_N_FEATURES];

PT_THREAD(<name>_demo_task)(struct pt *pt, void *arg) {
    (void)arg;
    PT_BEGIN(pt);
    while (1) {
        /* 用户接入：采集 N_CH 通道 × WIN_LEN 点 */
        if (algo_<name>_feat_extract(&s_ch_buf[0][0],
                                     ALGO_<NAME>_WIN_LEN, s_features) == 0) {
            int id = algo_<name>_predict(s_features,
                                         ALGO_<NAME>_N_FEATURES, NULL);
            if (id >= 0) {
                b_log_i("<name>: class=%d\r\n", id);
            }
        }
        PT_DELAY_MS(pt, 100);
    }
    PT_END(pt);
}
BOS_REG_POLLING_FUNC(<name>_demo_task);
```

表格模式直接：

```c
int id = algo_<name>_predict(features, ALGO_<NAME>_N_FEATURES, NULL);
```

要点：

- `features` 传 **未归一化** 特征；predict 内部用烘焙的 `s_offset/s_inv_scale` 调用 `bAlgoMlNormalize`。
- 需要类别名时用 `algo_<name>_class_names[id]`。
- PT 任务内跨 yield 状态必须 `static`（见仓库 CLAUDE.md）。

### 10.8 深度绑定：`bAlgo*` 原语

导出代码 **不是** Python 移植，而是编排固件算法库：

| 生成侧调用 | 固件头文件 | Kconfig |
|---|---|---|
| `bAlgoSignalStats/Mean/Std/Rms/Zcr/Skew/Kurt/Ptp/...` | `bos/algorithm/inc/algo_signal.h` | `_ALGO_SIGNAL_ENABLE` |
| `bAlgoFft/Magnitude/Centroid/Energy/DominantFreq/BandRatio/GenTwiddle/GenBitReverse` | `bos/algorithm/inc/algo_fft.h` | `_ALGO_FFT_ENABLE`，`ALGO_FFT_MAX_N=1024` |
| `bAlgoMlNormalize/Argmax/Softmax/Dot/Relu/Sigmoid/Exp/TreePredict` | `bos/algorithm/inc/algo_ml.h` | `_ALGO_ML_ENABLE`，`ALGO_ML_TREE_MAX_DEPTH=128` |

特征生成形态（`feature_cgen.py`）：

```c
bAlgoSignalStats_t s_stats;
bAlgoSignalStats(x, N, &s_stats);
out[oi++] = bAlgoSignalMean(&s_stats, N);
out[oi++] = bAlgoSignalRms(&s_stats, N);
/* 频域: 烘焙 twiddle/bit-reverse 表 */
bAlgoFft(s_re, s_im, N, tw_re, tw_im, rev);
bAlgoFftMagnitude(s_re, s_im, s_mag, N);
out[oi++] = bAlgoFftCentroid(s_mag, N, FS);
out[oi++] = bAlgoFftBandRatio(s_mag, N, lo, hi);
```

模型生成形态（`model_cgen.py`）：

```c
bAlgoMlNormalize(xf, features, s_offset, s_inv_scale, ALGO_<NAME>_N_FEATURES);
/* 树模型 */
int off = bAlgoMlTreePredict(nodes, nnodes, xf, nf);
/* LR/NB/MLP */
float z = bias + bAlgoMlDot(w, xf, nf);
bAlgoMlSoftmax(out, z, nc);
/* argmax 与 sklearn 一致：并列取最小索引 */
```

链接段（`section.txt`）：

| 符号 | 段 |
|---|---|
| `BOS_REG_POLLING_FUNC` 任务函数 | `.bos_polling` |
| 模型权重 / 节点表 / 归一化参数 | `.rodata` |
| FFT 表（大表） | `.bss` / `.fastram` |
| `predict` / `feat_extract` | `.text` |

### 10.9 AutoML API 速查

| 方法 | 路径 |
|---|---|
| GET/POST | `/api/projects` |
| GET/PATCH/DELETE | `/api/projects/{pid}` |
| POST | `/api/projects/import`（`.bosml`） |
| POST | `/api/projects/{pid}/archive`（导出 `.bosml`） |
| GET | `/api/projects/trash` 等回收站接口 |
| POST | `/api/projects/{pid}/dataset` 等（见 §10.2–10.6） |

---

## 11. 测试与排错

### 11.1 设备功能测试（主机侧）

```bash
cd tool/babyos-studio
python/.venv/bin/python -m pytest test/device_features/ -v
# 或
python/.venv/bin/python test/device_features/test_protocol_core.py
```

| 测试文件 | 覆盖 |
|---|---|
| `test_protocol_core.py` | 帧 pack/parse、校验和、TEA、0x3/0x4/0x6/0x7/0x8/0xA 负载、CRC32、Ymodem CRC16 向量、SN |
| `test_device_services.py` | ProtocolClient / Shell / HttpMock / DeviceManager / xmodem 状态机 |
| `test_device_api.py` | FastAPI 路由：串口、协议、OTA/文件异步 job、param、http、xmodem、结构化错误 |
| `test_e2e_pty.py` | PTY 环回 + `mock_babyos_device.py` 协议设备端到端 |
| `mock_babyos_device.py` | 主机侧 BabyOS 协议模拟设备（无硬件） |

AutoML / 导出相关：

```bash
# 服务层回归（pytest 无法直接收集 test_regression.py：需 PYTHONPATH 指向 python/）
cd tool/babyos-studio && PYTHONPATH=python python/.venv/bin/python test/test_regression.py
# 预期：结果: 46 通过, 0 失败

# 以下两个是「对运行中后端的 HTTP 联调脚本」，不是 pytest 用例
# （pytest 对其收集 0 项；必须先启动后端 :18080 再执行）
cd tool/babyos-studio/python
.venv/bin/python test_automl.py
.venv/bin/python test_api_pipeline.py

# 固件侧 bundle 自检样例
# test/automl_e2e/ 下的 test_export_pipeline.py / test_feature_parity.py 等
```

### 11.2 无串口硬件时如何验证「真实功能」

| 功能 | 无硬件验证方式 |
|---|---|
| b_protocol 帧 | `test_protocol_core.py` 向量 + `pack/parse` 往返 |
| OTA / 0x6 文件 | `test_device_api.py` + mock 设备状态机 |
| Xmodem/Ymodem | CRC 向量 + PTY/mock 接收端 |
| Shell param | mock shell 文本 |
| HTTP Mock | 本机真实 HTTP + `/_requests` 日志 + proxy |
| AutoML | CSV → 特征 → 训练 → 导出 bundle + 数值自检 |
| 固件集成 | `test/automl_e2e` 编译/符号/深度绑定核对 |

有硬件时：打开真实串口后，协议测试应返回 `param_text="BabyOS"`；OTA/文件 result_code 应为 `0`。

### 11.3 常见问题

| 现象 | 处理 |
|---|---|
| `SERIAL_NOT_OPEN` 409 | 先打开串口；确认是 Python 持有（协议功能可用） |
| `PROTOCOL_TIMEOUT` 504 | 波特率/接线/TEA 加密与固件不一致；设备未跑 `bModProtocol` |
| OTA result=1 CRC 错 | 固件 CRC 口径必须是 §3.3 反射 CRC32；勿用其它 CRC32 变体 |
| OTA result=2 名不匹配 | UI「固件名称」与设备期望名一致 |
| Ymodem 校验失败 | 使用 BabyOS 传输 CRC16（`0x2672` 向量），**不要**用标准 XMODEM `0x31C3` |
| SN 写入异常 | 确认未二次包长度前缀；UID 为原始字节 md5 |
| param 设置失败 | 固件是否启用 shell；值是否为 `atoi` 可解析整数；打开 `verify` 看回读 |
| HTTP Mock 设备访问不到 | Mock 只绑 `127.0.0.1`；设备需能路由到主机 IP，或改固件 HTTP 目标；实验室可用 proxy 验证 Mock 配置 |
| HTTPS Mock 失败 | 需要本机 `openssl` |
| 训练中改数据/特征 409 | 等待或取消训练 |
| 导出 TREE_TOO_DEEP / WIN_LEN 超限 | 树深 >128（`ALGO_ML_TREE_MAX_DEPTH`）在 `model_cgen` 以 `ValueError(TREE_TOO_DEEP:…)` 抛出，导出失败（HTTP 422，`detail.code=UNHANDLED`，detail 含 TREE_TOO_DEEP）。FFT 点数 >1024（`ALGO_FFT_MAX_N`）**无导出期硬拦截**：固件 `bAlgoFft` 运行时返回错误，导出自检③特征链会因 `feat_extract` 非 0 而失败。频域特征要求 N 为 2 的幂（`FREQ_NEEDS_POW2` 422） |
| Electron 原生串口协议不可用 | 必须改用 Python 后端串口（start_dev.sh 已保证后端启动） |

调试入口：

- UI 日志面板（Serial/Protocol/OTA/Xfer/HTTP/Shell/AutoML/Python）。
- `GET /api/device/logs?tail=200`
- `GET /api/device/status`

---

## 12. 功能状态表

| 功能 | 状态 | 真实实现位置 | 无硬件验证 |
|---|---|---|---|
| 串口枚举/打开/关闭 | 已实现 | `uart_service.py` + `/api/device/serial/*` | mock 串口路径校验 |
| b_protocol 帧 pack/parse/TEA | 已实现 | `b_protocol.py` | 单元测试向量 |
| 协议测试 0x1 / 设时间 0x2 | 已实现 | `protocol_client.py` + mock 设备 | PTY/mock E2E |
| OTA 0x3/0x4/0x5 | 已实现 | `start_ota` + async job API | mock 设备状态机 |
| 文件传输 0x6 + 0x4/0x5 | 已实现（API） | `start_file_transfer` + `/api/device/file/*` | mock 设备状态机 |
| Xmodem-128 | 已实现 | `xmodem_ydmodem.py` | CRC 向量 + mock 接收 |
| Ymodem-1K | 已实现 | 同上 | CRC 向量 + mock 接收 |
| UID 0x7 / SN 0x8 / DEVINFO 0xA | 已实现 | `protocol_client.py` + `sn_util.py` | mock 设备 |
| Shell 参数调节 | 已实现 | `shell_client.py` | mock shell |
| HTTP Mock + 请求日志 | 已实现 | `http_mock.py` | 本机真实 HTTP |
| 主机 HTTP 代理代发 | 已实现 | `/api/device/http/proxy` | 本机真实 HTTP |
| 设备侧 HTTP 触发（协议命令） | **未实现为 b_protocol** | 由固件 bHttp/工程代码发起 | 需固件 |
| UI「HTTP 初始化/反初始化客户端」按钮 | **无后端端点** | 仅 UI 按钮 | 不可用 |
| AutoML 工程/数据/标注/特征/训练 | 已实现 | `/api/projects/*` | CSV + pytest |
| C bundle 导出 + 自检 | 已实现 | `generator.py` / `export_service.py` | `export_report.json` |
| 深度绑定 `bAlgoSignal*`/`bAlgoFft*`/`bAlgoMl*` | 已实现 | `feature_cgen.py` / `model_cgen.py` + 固件头文件 | `test/automl_e2e` |
| Gitee 仓库快捷链接 | 已实现 | UI shell 打开外链 | 无需硬件 |

**原则**：上表「已实现」均有对应源码与测试路径；未实现项明确标注，不在 UI/API 中假装成功。

---

## 附录 A. 相关链接

- BabyOS 官方文档：https://babyos.cn/doc/
- BabyOS Gitee：https://gitee.com/notrynohigh/BabyOS
- 协议权威文档：仓库 **master** 分支 `tool/README.md`
- 固件算法：`bos/algorithm/`

## 附录 B. 目录结构

```
tool/babyos-studio/
├── electron/           # Electron 主进程 main.js / preload.js
├── ui/                 # index.html / style.css / app.js
├── python/
│   ├── app/
│   │   ├── api/        # projects, datasets, labels, features, training, export, device, ...
│   │   └── services/   # automl, trainer, feature_service, export/, ...
│   └── device/         # b_protocol, protocol_client, uart, shell, http_mock, xmodem
├── test/
│   ├── device_features/
│   └── test_regression.py 等
├── start_dev.sh / start_dev.bat
└── package.json
```
