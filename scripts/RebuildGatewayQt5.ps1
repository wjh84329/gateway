# Gateway only: CMake + Ninja + Qt5 static. Does NOT compile Qt from source.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\RebuildGatewayQt5.ps1
# When tools\7za.exe exists, CMake embeds it into the gateway exe (see CMakeLists.txt). Each run reconfigures (-S -B inside inner script).
# If the same -B folder was ever configured with Qt6, delete that build directory first; mixed caches often break configure/link for Qt5.

param(
    [string] $StaticQt = 'D:\Qt\5.15.15\msvc2019_64_static',
    [string] $BuildDirName = 'out\build_static_release_qt5'
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$BuildDir = Join-Path $ProjectDir $BuildDirName

Write-Host ''
Write-Host '[gateway-qt5] Gateway CMake only — no Qt source / qt-build.' -ForegroundColor Cyan
Write-Host ''

if (-not (Test-Path (Join-Path $ProjectDir 'CMakeLists.txt'))) {
    Write-Error "CMakeLists.txt missing: $ProjectDir"
}
$qt5cfg = Join-Path $StaticQt 'lib\cmake\Qt5\Qt5Config.cmake'
if (-not (Test-Path $qt5cfg)) {
    Write-Error "Qt5Config.cmake missing: $qt5cfg"
}

$vsDev = @(
    'D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat',
    'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat'
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vsDev) { Write-Error 'VsDevCmd.bat not found.' }

$cmake = @(
    'D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $cmake) { Write-Error 'cmake.exe not found under Visual Studio.' }

$inner = Join-Path $env:TEMP ("gwqt5_inner_{0}.ps1" -f [guid]::NewGuid().ToString('n'))
@'
param(
  [string] $CMakeExe,
  [string] $ProjectDir,
  [string] $BuildDir,
  [string] $StaticQt
)
$ErrorActionPreference = 'Stop'
$qtBin = Join-Path $StaticQt 'bin'
$parts = [Environment]::GetEnvironmentVariable('PATH', 'Process') -split ';' |
  Where-Object { $_ -and ($_ -notmatch '(?i)qt-build') -and ($_ -notmatch '(?i)qt-everywhere-src') }
$env:PATH = $qtBin + ';' + ($parts -join ';')
Write-Host '[gateway-qt5] PATH cleaned (no qt-build / qt-everywhere-src entries).'
& $CMakeExe -S $ProjectDir -B $BuildDir -G Ninja "-DCMAKE_PREFIX_PATH=$StaticQt" -DCMAKE_BUILD_TYPE=Release -DGATEWAY_QT5_MT_RUNTIME=ON
if (-not $?) { exit 1 }
& $CMakeExe --build $BuildDir
if (-not $?) { exit 1 }
exit 0
'@ | Set-Content -LiteralPath $inner -Encoding UTF8

try {
    $psArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$inner`" -CMakeExe `"$cmake`" -ProjectDir `"$ProjectDir`" -BuildDir `"$BuildDir`" -StaticQt `"$StaticQt`""
    $cmdLine = "call `"$vsDev`" -arch=x64 -host_arch=x64 >nul && powershell $psArgs"
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList @('/d', '/c', $cmdLine) -Wait -NoNewWindow -PassThru
    if ($p.ExitCode -ne 0) {
        Write-Error "Build failed, exit $($p.ExitCode)"
    }
} finally {
    Remove-Item -LiteralPath $inner -Force -ErrorAction SilentlyContinue
}

# OUTPUT_NAME 为中文；避免脚本文件编码与控制台代码页导致 Test-Path 误判
$builtExe = Get-ChildItem -LiteralPath $BuildDir -Filter '*.exe' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $builtExe) { Write-Error "Missing gateway .exe under $BuildDir" }
Write-Host ''
Write-Host ('[OK] ' + $builtExe.FullName) -ForegroundColor Green
