# BabyOS Studio

BabyOS 统一调试工具 — 纯桌面应用，无服务器依赖。

## 功能

- **串口控制**：串口连接、协议测试、数据收发
- **OTA 升级**：固件选择、升级进度监控
- **Xmodem/Ymodem**：文件传输协议支持
- **HTTP 调试**：Mock 服务器、设备请求触发
- **参数调节**：Shell 参数读写、定时轮询
- **设备信息**：UID、SN、设备信息获取
- **AutoML 项目**：数据导入、标注、特征工程、模型训练、导出
- **Gitee 仓库**：一键直达 BabyOS 源码仓库

## 快速开始

### 开发模式

```bash
# Linux/macOS
./start_dev.sh

# Windows
start_dev.bat
```

脚本会自动：
1. 检查并安装 Node.js（如未安装）
2. 检查并安装 Python（如未安装）
3. 创建虚拟环境并安装依赖
4. 直接启动 Electron 桌面应用

### 构建发布版

```bash
# 安装依赖
npm install

# 构建所有平台
npm run build

# 仅构建 Windows
npm run build:win

# 仅构建 macOS
npm run build:mac

# 仅构建 Linux
npm run build:linux
```

## 技术栈

- **桌面框架**：Electron 28
- **UI**：原生 HTML/CSS/JavaScript（无框架）
- **串口通信**：serialport (Node.js)
- **后端**：Python FastAPI (可选，用于 AutoML)

## 项目结构

```
babyos-studio/
├── electron/           # Electron 主进程
│   ├── main.js         # 主进程入口
│   └── preload.js      # 预加载脚本
├── ui/                 # 界面文件（纯 HTML/CSS/JS）
│   ├── index.html      # 主界面
│   ├── style.css       # 样式
│   └── app.js          # 应用逻辑
├── python/             # Python 后端 (AutoML, 可选)
├── package.json
├── start_dev.sh        # 启动脚本 (Linux/macOS)
└── start_dev.bat       # 启动脚本 (Windows)
```

## 相关链接

- [BabyOS 官方文档](https://babyos.cn/doc/)
- [BabyOS Gitee 仓库](https://gitee.com/notrynohigh/BabyOS)
