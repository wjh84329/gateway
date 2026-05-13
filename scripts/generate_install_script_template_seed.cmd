@echo off
setlocal
REM 默认通过 pwsh 执行，避免 Windows PowerShell 5.x 读文件编码不一致
where pwsh >nul 2>&1
if errorlevel 1 (
  echo [错误] 未找到 pwsh。请安装 PowerShell 7+ 并确保已加入 PATH。
  echo 下载: https://github.com/PowerShell/PowerShell/releases
  exit /b 1
)
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0generate_install_script_template_seed.ps1" %*
exit /b %ERRORLEVEL%
