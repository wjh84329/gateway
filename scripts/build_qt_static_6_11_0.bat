@echo off
chcp 65001 >nul
setlocal ENABLEEXTENSIONS

REM ==================================================
REM Qt 6.11.0 static build script
REM Configure / build / install static Qt
REM Safe: no delete, no cleanup, no business program execution
REM ==================================================

set "QT_SOURCE_DIR=D:\qt-everywhere-src-6.11.0"
set "QT_BUILD_DIR=D:\qt-build-6.11.0-static-x64"
set "QT_INSTALL_DIR=D:\Qt\6.11.0\msvc2022_64_static"

set "VS_DEV_CMD="
if exist "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if not defined VS_DEV_CMD if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"

if not defined VS_DEV_CMD (
    echo [错误] 未找到 VsDevCmd.bat，请先安装 Visual Studio C++ 桌面开发环境。
    exit /b 1
)

call "%VS_DEV_CMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
    echo [错误] 加载 VS x64 编译环境失败。
    exit /b 1
)

set "CMAKE_EXE="
if exist "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE for /f "delims=" %%I in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"

if not defined CMAKE_EXE (
    echo [错误] 未找到 cmake.exe。
    exit /b 1
)

if not exist "%QT_SOURCE_DIR%\configure.bat" (
    echo [错误] 未找到 Qt 源码目录：%QT_SOURCE_DIR%
    exit /b 1
)

if not exist "%QT_BUILD_DIR%" mkdir "%QT_BUILD_DIR%"
cd /d "%QT_BUILD_DIR%"

echo ========================================
echo   Qt 6.11.0 静态版一键构建
echo ========================================
echo 源码目录：%QT_SOURCE_DIR%
echo 构建目录：%QT_BUILD_DIR%
echo 安装目录：%QT_INSTALL_DIR%
echo.

call "%QT_SOURCE_DIR%\configure.bat" -prefix "%QT_INSTALL_DIR%" -release -static -static-runtime -submodules qtbase,qttools -nomake examples -nomake tests -opensource -confirm-license -cmake-generator Ninja
if errorlevel 1 (
    echo [错误] Qt configure 失败。
    exit /b 1
)

"%CMAKE_EXE%" --build . --parallel
if errorlevel 1 (
    echo [错误] Qt 编译失败。
    exit /b 1
)

"%CMAKE_EXE%" --install .
if errorlevel 1 (
    echo [错误] Qt 安装失败。
    exit /b 1
)

echo.
echo [成功] Qt 6.11.0 静态版已安装到：%QT_INSTALL_DIR%
exit /b 0