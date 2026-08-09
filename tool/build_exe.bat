@echo off
REM ============================================================
REM  BabyOS Upper-PC Packager (PyInstaller, single-file exe)
REM ============================================================
REM  Build process:
REM    1. Ensure tool/__init__.py exists (PyInstaller needs it to scan
REM       tool/ as a package and find all .py modules)
REM    2. Run PyInstaller from BabyOS/ (parent of tool/) with:
REM         --collect-submodules tool  (find all tool/* modules)
REM         --runtime-hook pyi_rth_alias.py  (alias tool.X -> X at runtime)
REM    3. Restore __init__.py if we touched it, move exe to tool/
REM
REM  Why the runtime hook?
REM    tool/ is bundled as a Python package, so modules are registered
REM    in sys.modules as "tool.b_protocol", "tool.mainwindow", etc.
REM    But main.py / mainwindow.py use BARE names like
REM    "from b_protocol import ..." which look up "b_protocol" in
REM    sys.modules and fail. The runtime hook copies each "tool.X" to
REM    a top-level "X" before user code runs.
REM
REM  Usage:
REM    Double-click build_exe.bat, or run from cmd.
REM  Output:
REM    BabyOS\tool\babyos_upper_pc.exe  (single-file, no console)
REM ============================================================

REM Switch console to UTF-8 so any Chinese echo prints correctly
chcp 65001 >nul

setlocal

REM Compute parent directory of this bat (i.e. BabyOS/)
set "SCRIPT_DIR=%~dp0"
set "PARENT_DIR=%SCRIPT_DIR%.."
pushd "%PARENT_DIR%"

echo ============================================
echo  BabyOS Upper PC - PyInstaller Packager
echo  Working dir: %CD%
echo ============================================
echo.

REM ---- 1. Check Python ----
where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found in PATH
    popd
    pause
    exit /b 1
)
python --version
echo.

REM ---- 2. Ensure deps ----
echo [1/4] Checking dependencies: pyinstaller / pyserial ...
python -c "import PyInstaller" 2>nul
if errorlevel 1 (
    echo   pyinstaller missing, installing ...
    pip install pyinstaller
    if errorlevel 1 (
        echo [ERROR] pip install pyinstaller failed
        popd
        pause
        exit /b 1
    )
)

python -c "import serial" 2>nul
if errorlevel 1 (
    echo   pyserial missing, installing ...
    pip install pyserial
    if errorlevel 1 (
        echo [ERROR] pip install pyserial failed
        popd
        pause
        exit /b 1
    )
)

REM ---- 3. Clean previous artifacts ----
echo.
echo [2/4] Cleaning previous artifacts ...
if exist tool\babyos_upper_pc.exe del /q tool\babyos_upper_pc.exe
if exist tool\babyos_upper_pc.spec del /q tool\babyos_upper_pc.spec
if exist tool\build rmdir /s /q tool\build
if exist tool\dist rmdir /s /q tool\dist

REM ---- 3b. Ensure tool/__init__.py exists (needed by --collect-submodules tool) ----
set "RESTORE_INIT=0"
if not exist tool\__init__.py (
    echo [2.5/4] Creating temporary tool\__init__.py for PyInstaller ...
    echo # Auto-created by build_exe.bat, will be removed > tool\__init__.py
    set "RESTORE_INIT=1"
)

REM ---- 4. Run PyInstaller ----
echo.
echo [3/4] Packaging (single-file, no console) ...
echo.

pyinstaller ^
    -F ^
    -w ^
    --name babyos_upper_pc ^
    --clean ^
    --noconfirm ^
    --paths tool ^
    --collect-submodules tool ^
    --collect-all tkinter ^
    --add-data "%SCRIPT_DIR%mock_https_cert.pem;." ^
    --add-data "%SCRIPT_DIR%mock_https_key.pem;." ^
    --hidden-import serial ^
    --hidden-import serial.tools.list_ports ^
    --runtime-hook pyi_rth_alias.py ^
    --distpath tool\dist ^
    --workpath tool\build ^
    --specpath tool ^
    tool\main.py

if errorlevel 1 (
    echo.
    echo [ERROR] PyInstaller failed
    if "%RESTORE_INIT%"=="1" if exist tool\__init__.py del /q tool\__init__.py
    popd
    pause
    exit /b 1
)

REM ---- 5. Move artifact to tool/ root ----
echo.
echo [4/4] Moving artifact to tool/ root ...
if exist tool\dist\babyos_upper_pc.exe (
    if exist tool\babyos_upper_pc.exe del /q tool\babyos_upper_pc.exe
    move /y tool\dist\babyos_upper_pc.exe tool\babyos_upper_pc.exe >nul
    if exist tool\dist rmdir /s /q tool\dist
    if exist tool\build rmdir /s /q tool\build
    if exist tool\babyos_upper_pc.spec del /q tool\babyos_upper_pc.spec

    REM Remove temp __init__.py if we created it
    if "%RESTORE_INIT%"=="1" if exist tool\__init__.py del /q tool\__init__.py

    echo   Output: %SCRIPT_DIR%babyos_upper_pc.exe
) else (
    echo [ERROR] tool\dist\babyos_upper_pc.exe not found
    if "%RESTORE_INIT%"=="1" if exist tool\__init__.py del /q tool\__init__.py
    popd
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Build OK!
echo  Output: %SCRIPT_DIR%babyos_upper_pc.exe
echo ============================================
echo.
echo  Usage:
echo    1) Copy babyos_upper_pc.exe anywhere
echo    2) Double-click to run
echo    3) On first run webconfig_tool.ini is auto-created
echo ============================================
echo.

popd
pause
endlocal