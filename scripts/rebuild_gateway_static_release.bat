@echo off
chcp 65001 >nul
setlocal ENABLEEXTENSIONS

REM ==================================================
REM gateway static Qt Release rebuild script
REM Reconfigure and rebuild project with installed static Qt
REM Safe: no delete, no app launch, no business action trigger
REM ==================================================

set "STATIC_QT_DIR=D:\Qt\6.11.0\msvc2022_64_static"
set "BUILD_DIR_NAME=out\build_static_release"

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_DIR=%%~fI"
set "BUILD_DIR=%PROJECT_DIR%\%BUILD_DIR_NAME%"

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

if not exist "%STATIC_QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [错误] 未找到静态 Qt 安装目录：%STATIC_QT_DIR%
    echo         请先完成 Qt 6.11.0 静态编译与安装。
    exit /b 1
)

echo ========================================
echo   gateway 静态 Qt Release 一键重编
echo ========================================
echo 项目目录：%PROJECT_DIR%
echo Qt 目录  ：%STATIC_QT_DIR%
echo 构建目录：%BUILD_DIR%
echo.

"%CMAKE_EXE%" -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_PREFIX_PATH="%STATIC_QT_DIR%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [错误] CMake 配置失败。
    exit /b 1
)

"%CMAKE_EXE%" --build "%BUILD_DIR%"
if errorlevel 1 (
    echo [错误] 项目编译失败。
    exit /b 1
)

if exist "%BUILD_DIR%\cs.exe" (
    echo.
    echo [成功] 已生成：%BUILD_DIR%\cs.exe
    echo        该文件为静态 Qt 版本，可用于“单文件直接运行”验证。
    exit /b 0
)

echo [错误] 编译完成后未找到 cs.exe
exit /b 1