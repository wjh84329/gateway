@echo off
chcp 65001 >nul
REM 批处理脚本：构建单文件可执行程序
REM 使用 windeployqt + Enigma Virtual Box

setlocal enabledelayedexpansion

set BUILD_CONFIG=Release
set OUTPUT_DIR=out\build\%BUILD_CONFIG%
set ENIGMA_VBOX_PATH=D:\Program Files (x86)\Enigma Virtual Box\enigmavb.exe

echo ========================================
echo   单文件打包工具
echo ========================================
echo.

REM 检查 Enigma Virtual Box 是否存在
if not exist "%ENIGMA_VBOX_PATH%" (
    echo [错误] Enigma Virtual Box 未找到: %ENIGMA_VBOX_PATH%
    echo 请从 https://enigmaprotector.com/en/files/enigmavb.html 下载并安装
    pause
    exit /b 1
)

REM 查找 Qt 路径
set QT_PATH=
if defined Qt5_DIR (
    for %%I in (%Qt5_DIR%\..) do set QT_PATH=%%~fI
)
if defined Qt6_DIR (
    for %%I in (%Qt6_DIR%\..) do set QT_PATH=%%~fI
)

if "%QT_PATH%"=="" (
    REM 尝试常见路径
    if exist "C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe" (
        set QT_PATH=C:\Qt\5.15.2\msvc2019_64
    ) else if exist "C:\Qt\6.5.0\msvc2019_64\bin\windeployqt.exe" (
        set QT_PATH=C:\Qt\6.5.0\msvc2019_64
    ) else if exist "C:\Qt\6.6.0\msvc2019_64\bin\windeployqt.exe" (
        set QT_PATH=C:\Qt\6.6.0\msvc2019_64
    )
)

if "%QT_PATH%"=="" (
    echo [错误] 无法找到 Qt 路径，请设置 Qt5_DIR 或 Qt6_DIR 环境变量
    pause
    exit /b 1
)

set WINDEPLOYQT=%QT_PATH%\bin\windeployqt.exe
set EXE_PATH=%OUTPUT_DIR%\cs.exe
set DEPLOY_DIR=%OUTPUT_DIR%\qt_plugins
set EVB_CONFIG=%OUTPUT_DIR%\evb_config.xml
set SINGLE_EXE=%OUTPUT_DIR%\cs_single.exe

REM 检查主程序是否存在
if not exist "%EXE_PATH%" (
    echo 正在构建项目...
    cmake --build out\build --config %BUILD_CONFIG%
    if errorlevel 1 (
        echo [错误] 构建失败
        pause
        exit /b 1
    )
)

echo 1. 部署 Qt 依赖...
if exist "%DEPLOY_DIR%" (
    rmdir /s /q "%DEPLOY_DIR%"
)
mkdir "%DEPLOY_DIR%"

"%WINDEPLOYQT%" --release --no-compiler-runtime --no-translations --no-opengl-sw --dir "%DEPLOY_DIR%" "%EXE_PATH%"
if errorlevel 1 (
    echo [错误] windeployqt 执行失败
    pause
    exit /b 1
)

echo 2. 生成 Enigma Virtual Box 配置...
(
echo ^<?xml version="1.0" encoding="utf-8"?^>
echo ^<enigmavb^>
echo     ^<output^>%SINGLE_EXE%^</output^>
echo     ^<input^>%EXE_PATH%^</input^>
echo     ^<packfiles^>
echo         ^<pack^>
echo             ^<folder^>%DEPLOY_DIR%^</folder^>
echo             ^<subfolders^>true^</subfolders^>
echo             ^<root^>%OUTPUT_DIR%^</root^>
echo         ^</pack^>
echo     ^</packfiles^>
echo     ^<compress^>true^</compress^>
echo     ^<compresslevel^>9^</compresslevel^>
echo     ^<temppath^>%%TEMP%%^</temppath^>
echo     ^<executionmode^>0^</executionmode^>
echo ^</enigmavb^>
) > "%EVB_CONFIG%"

echo 3. 打包单文件...
"%ENIGMA_VBOX_PATH%" pack "%EVB_CONFIG%"
if errorlevel 1 (
    echo [错误] Enigma Virtual Box 打包失败
    pause
    exit /b 1
)

if exist "%SINGLE_EXE%" (
    echo.
    echo ========================================
    echo   打包完成!
    echo ========================================
    echo   单文件版: %SINGLE_EXE%
    echo.
) else (
    echo [错误] 打包失败，未生成单文件
    pause
    exit /b 1
)

endlocal