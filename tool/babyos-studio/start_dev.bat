@echo off
REM 强制 UTF-8 代码页（bat 文件以 UTF-8 保存，无 BOM；不加 chcp 在非中文 Windows 上中文会乱码）
chcp 65001 >nul 2>&1
REM 强制 Python 子进程使用 UTF-8（pip 读 requirements.txt 含中文注释时不再 GBK 报错）
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"
REM pip 用清华源，避免默认 PyPI 在国内超时（ReadTimeoutError）
set "PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple"
set "PIP_TRUSTED_HOST=pypi.tuna.tsinghua.edu.cn"
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo.
echo ==========================================
echo   BabyOS Studio — 环境检查与安装
echo ==========================================
echo.

REM ============================================
REM 1. 检查并安装 Node.js
REM ============================================
set NODE_OK=0

where node >nul 2>nul
if %errorlevel% equ 0 (
    for /f "tokens=1 delims=." %%a in ('node --version') do set NODE_VER=%%a
    set NODE_VER=!NODE_VER:v=!
    if !NODE_VER! geq 18 (
        echo [OK] Node.js 已安装
        node --version
        set NODE_OK=1
    ) else (
        echo [WARN] Node.js 版本过低（需要 18+，当前 !NODE_VER!）
    )
)

if %NODE_OK% equ 0 (
    echo.
    echo ------------------------------------------
    echo  Node.js 18+ 未找到，需要安装才能运行
    echo ------------------------------------------
    echo.
    echo  推荐安装方式（任选其一）:
    echo.
    echo    方式 1: 访问 https://nodejs.org/ 下载 LTS 版本
    echo            安装时勾选 "Add to PATH"（自动添加环境变量）
    echo.
    echo    方式 2: 如果已安装 winget，可直接在下方输入 Y 自动安装
    echo.
    echo    方式 3: 如果已安装 nvm-windows，可手动执行:
    echo            nvm install 18 ^&^& nvm use 18
    echo.

    where winget >nul 2>nul
    if !errorlevel! equ 0 (
        echo  检测到 winget，是否自动安装 Node.js LTS? [Y/N]
        set /p INSTALL_NODE=  请输入:
        if /i "!INSTALL_NODE!"=="Y" (
            echo.
            echo [INFO] 正在通过 winget 安装 Node.js LTS ...
            echo        安装过程中可能弹出安装向导，请按提示操作。
            echo.
            winget install OpenJS.NodeJS.LTS --accept-package-agreements --accept-source-agreements
            if !errorlevel! equ 0 (
                echo.
                echo [OK] Node.js 安装完成
                REM 刷新当前会话 PATH
                set "PATH=%LOCALAPPDATA%\Programs\nodejs;%PATH%"
                set "PATH=%APPDATA%\npm;%PATH%"
                set NODE_OK=1
            ) else (
                echo [WARN] winget 安装失败或被取消
            )
        )
    ) else (
        echo  未检测到 winget，请手动安装 Node.js 18+
        echo  下载地址: https://nodejs.org/
        echo.
    )

    REM 最终检查
    if !NODE_OK! equ 0 (
        echo.
        echo ==========================================
        echo  安装 Node.js 后，请执行以下操作:
        echo.
        echo  1. 如果安装时未勾选 "Add to PATH":
        echo     手动将 Node.js 安装目录添加到系统环境变量 PATH
        echo     默认路径: %%LOCALAPPDATA%%\Programs\nodejs
        echo.
        echo  2. 添加后重新打开此脚本，或执行:
        echo     set PATH=%%LOCALAPPDATA%%\Programs\nodejs;%%PATH%%
        echo.
        echo  3. 验证安装:
        echo     node --version
        echo ==========================================
        echo.
        pause
        exit /b 1
    )

    REM 安装后验证
    echo.
    echo [INFO] 验证 Node.js 安装...
    node --version
    if !errorlevel! neq 0 (
        echo.
        echo [WARN] node 命令不可用，可能需要重启终端或添加环境变量。
        echo [INFO] 请打开新的命令行窗口再运行此脚本。
        echo.
        pause
        exit /b 1
    )
)

echo.

REM ============================================
REM 2. 检查 Python（可选）
REM ============================================
set PYTHON_CMD=

REM Windows 上优先用 python（python3 在 Windows 上常是 MS Store shim，venv 会假成功）
where python >nul 2>nul
if !errorlevel! equ 0 (
    set PYTHON_CMD=python
    goto :python_found
)
where python3 >nul 2>nul
if !errorlevel! equ 0 (
    set PYTHON_CMD=python3
    goto :python_found
)
echo [WARN] 未找到 Python（AutoML 后端需要，可选）
echo [INFO] 如需 AutoML 功能，请安装 Python 3.8+: https://python.org/
echo.
goto :python_done

:python_found
echo [OK] Python 已安装
%PYTHON_CMD% --version
echo.

if not exist "python\.venv\Scripts\python.exe" (
    REM venv 不存在或已损坏（仅 .venv 目录但无 Scripts\python.exe）→ 重建
    if exist "python\.venv" (
        echo [WARN] 检测到破损的虚拟环境，正在清理重建 ...
        rmdir /s /q "python\.venv"
    )
    echo [INFO] 创建 Python 虚拟环境 ...
    %PYTHON_CMD% -m venv python\.venv
    if exist "python\.venv\Scripts\pip.exe" (
        python\.venv\Scripts\pip.exe install -q --upgrade pip
    )
)
if not exist "python\.venv\.deps_installed" (
    echo [INFO] 安装 Python 依赖（首次运行，可能需要几分钟）...
    if exist "python\.venv\Scripts\pip.exe" (
        python\.venv\Scripts\pip.exe install -r python\requirements.txt
        if !errorlevel! equ 0 (
            echo. > python\.venv\.deps_installed
            echo [OK] Python 依赖安装完成
        ) else (
            echo [WARN] Python 依赖安装失败，AutoML 功能可能不可用
        )
    ) else (
        echo [WARN] pip 不存在，跳过依赖安装
    )
) else (
    echo [OK] Python 依赖已安装
)
echo.

:python_done

REM ============================================
REM 3. 安装 Node.js 依赖
REM ============================================
if not exist "node_modules" (
    echo [INFO] 安装 Node.js 依赖（首次运行，可能需要几分钟）...
    set ELECTRON_MIRROR=https://npmmirror.com/mirrors/electron/
    set electron_config_cache=%USERPROFILE%\.electron-cache
    call npm install
    if !errorlevel! equ 0 (
        echo [OK] Node.js 依赖安装完成
    ) else (
        echo [WARN] npm install 失败，尝试使用国内镜像...
        call npm install --registry=https://registry.npmmirror.com
        if !errorlevel! equ 0 (
            echo [OK] Node.js 依赖安装完成（使用国内镜像）
        ) else (
            echo [ERROR] 依赖安装失败，请检查网络后重试
            pause
            exit /b 1
        )
    )
) else (
    echo [OK] Node.js 依赖已安装
)
echo.

REM ============================================
REM 4. 启动 Electron
REM ============================================
echo ==========================================
echo   启动 BabyOS Studio
echo ==========================================
echo.

call npm run dev
