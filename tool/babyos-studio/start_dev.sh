#!/usr/bin/env bash
# BabyOS Studio 启动脚本
# 纯桌面应用，无服务器依赖
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_ok()   { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $1"; }
log_info() { echo -e "[INFO] $1"; }

# 加载 nvm
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh" 2>/dev/null || true

echo "=========================================="
echo "  BabyOS Studio — 环境检查"
echo "=========================================="
echo ""

# 1. 检查 Node.js
check_node() {
    if command -v node &> /dev/null; then
        local ver
        ver=$(node --version | sed 's/v//' | cut -d. -f1)
        if [ "$ver" -ge 18 ]; then
            log_ok "Node.js $(node --version)"
            return 0
        fi
    fi
    return 1
}

install_node() {
    log_info "尝试自动安装 Node.js ..."
    if [ ! -f "$HOME/.nvm/nvm.sh" ]; then
        if command -v wget &> /dev/null; then
            log_info "安装 nvm ..."
            wget -q -O /tmp/install_nvm.sh https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh
            bash /tmp/install_nvm.sh
            rm -f /tmp/install_nvm.sh
            export NVM_DIR="$HOME/.nvm"
            [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
        else
            log_err "需要 wget 来安装 nvm"
            return 1
        fi
    fi
    if [ -f "$HOME/.nvm/nvm.sh" ]; then
        export NVM_DIR="$HOME/.nvm"
        . "$HOME/.nvm/nvm.sh"
        nvm install 18
        nvm use 18
        log_ok "通过 nvm 安装 Node.js 18 完成"
        return 0
    fi
    log_err "无法自动安装 Node.js，请手动安装 18+"
    return 1
}

if ! check_node; then
    install_node || exit 1
fi

# 2. 检查 Python（可选）
PYTHON_CMD=""
if command -v python3 &> /dev/null; then
    PYTHON_CMD="python3"
    log_ok "Python3 $(python3 --version 2>&1)"
elif command -v python &> /dev/null; then
    PYTHON_CMD="python"
    log_ok "Python $(python --version 2>&1)"
else
    log_warn "未找到 Python（AutoML 后端需要，可选）"
fi

if [ -n "$PYTHON_CMD" ]; then
    PYTHON_DIR="$SCRIPT_DIR/python"
    if [ ! -d "$PYTHON_DIR/.venv" ]; then
        log_info "创建 Python 虚拟环境 ..."
        $PYTHON_CMD -m venv "$PYTHON_DIR/.venv"
        "$PYTHON_DIR/.venv/bin/pip" install -q --upgrade pip
    fi
    if [ ! -f "$PYTHON_DIR/.venv/.deps_installed" ]; then
        log_info "安装 Python 依赖 ..."
        "$PYTHON_DIR/.venv/bin/pip" install -q -r "$PYTHON_DIR/requirements.txt"
        touch "$PYTHON_DIR/.venv/.deps_installed"
        log_ok "Python 依赖安装完成"
    else
        log_ok "Python 依赖已安装"
    fi
fi

# 3. 安装 Node.js 依赖
if [ ! -d node_modules ]; then
    log_info "安装 Node.js 依赖 ..."
    export ELECTRON_MIRROR="https://npmmirror.com/mirrors/electron/"
    export electron_config_cache="$HOME/.electron-cache"
    if npm install; then
        log_ok "依赖安装完成"
    else
        log_warn "npm install 失败，尝试使用国内镜像..."
        if npm install --registry=https://registry.npmmirror.com; then
            log_ok "依赖安装完成（使用国内镜像）"
        else
            log_err "依赖安装失败，请检查网络后重试"
            exit 1
        fi
    fi
else
    log_ok "依赖已安装"
fi

# 4. 启动 Electron
echo ""
echo "=========================================="
echo "  启动 BabyOS Studio"
echo "=========================================="
echo ""
log_info "启动 Electron 桌面应用..."
echo ""

npm run dev
