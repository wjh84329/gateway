@echo off

chcp 65001 >nul

REM 批处理脚本：构建单文件可执行程序

REM 使用 windeployqt + Enigma Virtual Box

REM 若已放置 tools\7za.exe 或 tools\7zr.exe，会先 cmake -S -B 再编译（嵌入资源需重新配置 CMake）



setlocal enabledelayedexpansion



set BUILD_CONFIG=Release

REM 脚本在 scripts\ 下，仓库根为上一级

pushd "%~dp0.."

set REPO_ROOT=%CD%

popd



set OUTPUT_DIR=%REPO_ROOT%\out\build\%BUILD_CONFIG%

set BUILD_DIR=%REPO_ROOT%\out\build

set ENIGMA_VBOX_PATH=D:\Program Files (x86)\Enigma Virtual Box\enigmavb.exe



echo ========================================

echo   单文件打包工具

echo   仓库: %REPO_ROOT%

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

    for %%I in ("%Qt5_DIR%\..") do set QT_PATH=%%~fI

)

REM 仅当未从 Qt5_DIR 得到路径时再使用 Qt6（避免“两个都设了却编成 Qt6”）

if "%QT_PATH%"=="" if defined Qt6_DIR (

    for %%I in ("%Qt6_DIR%\..") do set QT_PATH=%%~fI

)



if "%QT_PATH%"=="" (

    if defined CMAKE_PREFIX_PATH set "QT_PATH=%CMAKE_PREFIX_PATH%"

)

if "%QT_PATH%"=="" (

    if exist "C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe" (

        set QT_PATH=C:\Qt\5.15.2\msvc2019_64

    ) else if exist "D:\Qt\6.11.0\msvc2022_64_static\bin\windeployqt.exe" (

        set QT_PATH=D:\Qt\6.11.0\msvc2022_64_static

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

set EXE_PATH=%OUTPUT_DIR%\支付网关.exe

set DEPLOY_DIR=%OUTPUT_DIR%\qt_plugins

set EVB_CONFIG=%OUTPUT_DIR%\evb_config.xml

set SINGLE_EXE=%OUTPUT_DIR%\cs_single.exe



set NEED_CMAKE=0

if exist "%REPO_ROOT%\tools\7za.exe" set NEED_CMAKE=1

if exist "%REPO_ROOT%\tools\7zr.exe" set NEED_CMAKE=1

if not exist "%BUILD_DIR%\CMakeCache.txt" set NEED_CMAKE=1

if "%NEED_CMAKE%"=="1" (

    echo 正在运行 CMake 配置（-DCMAKE_PREFIX_PATH=%QT_PATH%）...

    pushd "%REPO_ROOT%"

    cmake -S . -B out\build -DCMAKE_PREFIX_PATH="%QT_PATH%"

    if errorlevel 1 (

        echo [错误] CMake 配置失败。请手动执行:

        echo   cmake -S . -B out\build -DCMAKE_PREFIX_PATH=你的Qt根目录

        popd

        pause

        exit /b 1

    )

    popd

) else (

    echo 已有 CMakeCache.txt；若刚放入 tools\7za.exe / tools\7zr.exe 仍未嵌入，请删除 out\build\CMakeCache.txt 后重跑。

)



echo 正在编译 %BUILD_CONFIG% ...

pushd "%REPO_ROOT%"

cmake --build out\build --config %BUILD_CONFIG%

if errorlevel 1 (

    echo [错误] 构建失败

    popd

    pause

    exit /b 1

)

popd



if not exist "%EXE_PATH%" (

    echo [错误] 未找到主程序: %EXE_PATH%

    pause

    exit /b 1

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

