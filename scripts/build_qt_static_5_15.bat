@echo off
chcp 65001 >nul
setlocal ENABLEEXTENSIONS

REM ==================================================
REM Qt 5.15.x static build (MSVC, out-of-source)
REM 与 Qt 6 不同：5.15 使用 configure.bat + jom/nmake，不用 cmake --build
REM 用途：静态单文件 exe；目标系统可含 Win7 / Server 2008 R2（需配合 gateway 用 Qt5 构建）
REM ==================================================

REM 按你本机修改这三行（版本号与解压路径一致即可；当前与 Qt 5.15.15 对齐）
set "QT_SOURCE_DIR=D:\qt-everywhere-src-5.15.15"
set "QT_BUILD_DIR=D:\qt-build-5.15.15-static-x64"
set "QT_INSTALL_DIR=D:\Qt\5.15.15\msvc2019_64_static"

set "VS_DEV_CMD="
if exist "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not defined VS_DEV_CMD if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not defined VS_DEV_CMD if exist "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if not defined VS_DEV_CMD if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if not defined VS_DEV_CMD if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"

if not defined VS_DEV_CMD (
    echo [错误] 未找到 VsDevCmd.bat。请安装 VS 2019/2022 并勾选「使用 C++ 的桌面开发」。
    exit /b 1
)

call "%VS_DEV_CMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
    echo [错误] 加载 VS x64 编译环境失败。
    exit /b 1
)

if not exist "%QT_SOURCE_DIR%\configure.bat" (
    echo [错误] 未找到 Qt 5.15 源码：%QT_SOURCE_DIR%\configure.bat
    echo        请从官方归档下载 qt-everywhere-opensource-src-5.15.x.zip 并解压到上述目录。
    echo        索引：https://download.qt.io/archive/qt/5.15/
    exit /b 1
)

if not exist "%QT_BUILD_DIR%" mkdir "%QT_BUILD_DIR%"
cd /d "%QT_BUILD_DIR%"

echo ========================================
echo   [Qt-src] Qt 5.15.x static build - configure, then jom or nmake
echo   For gateway exe use rebuild_gateway_static_release_qt5.bat only.
echo ========================================
echo 源码目录：%QT_SOURCE_DIR%
echo 构建目录：%QT_BUILD_DIR%
echo 安装目录：%QT_INSTALL_DIR%
echo.

REM -sql-odbc：gateway 使用 QODBC
REM -skip qtwebengine -skip qt3d：显著缩短时间与体积；可按需增删 -skip
call "%QT_SOURCE_DIR%\configure.bat" ^
    -prefix "%QT_INSTALL_DIR%" ^
    -release ^
    -static ^
    -static-runtime ^
    -opensource ^
    -confirm-license ^
    -nomake examples ^
    -nomake tests ^
    -sql-odbc ^
    -skip qtwebengine ^
    -skip qt3d

if errorlevel 1 (
    echo [错误] Qt configure 失败。
    exit /b 1
)

where jom >nul 2>&1
if not errorlevel 1 (
    echo [信息] 使用 jom 并行编译（推荐）。
    jom
    if errorlevel 1 (
        echo [错误] jom 编译失败。
        exit /b 1
    )
    jom install
    if errorlevel 1 (
        echo [错误] jom install 失败。
        exit /b 1
    )
) else (
    echo [提示] 未在 PATH 中找到 jom，改用 nmake（很慢）。建议安装 jom：https://wiki.qt.io/Jom
    nmake
    if errorlevel 1 (
        echo [错误] nmake 编译失败。
        exit /b 1
    )
    nmake install
    if errorlevel 1 (
        echo [错误] nmake install 失败。
        exit /b 1
    )
)

echo.
echo [成功] Qt 5.15 静态版已安装到：%QT_INSTALL_DIR%
echo        下一步：执行 scripts\rebuild_gateway_static_release_qt5.bat（并确保 gateway 已按 Qt5 能编过）
exit /b 0
