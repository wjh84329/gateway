@echo off
chcp 65001 >nul
setlocal ENABLEEXTENSIONS

REM 静态 Qt5：CMake+Ninja 重编网关（默认输出 out\build_static_release_qt5\支付网关.exe）。
REM 内嵌 UPDATE.exe / 7za 等均打进上述 exe（与 out\build 无关）。
REM 若还要 Enigma 的 cs_single.exe：请用 build_single_file.ps1 且指定与这里相同的 -CMakeBuildDir（见脚本注释），勿沿用默认 out\build 否则会打到另一套产物。
REM 若存在 tools\7za.exe，CMake 会将 7za 嵌入 exe（在线更新 zip 解压）；本脚本每次都会 -S -B，无需单独为嵌入再配一次。

echo.
echo [gateway-qt5] Gateway CMake only — NOT Qt source. PATH is cleaned in PowerShell.
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0RebuildGatewayQt5.ps1" %*
set "EC=%ERRORLEVEL%"
exit /b %EC%
