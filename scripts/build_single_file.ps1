# PowerShell 脚本：构建单文件可执行程序
# 使用 windeployqt + Enigma Virtual Box
# 注意：放入 tools\7za.exe 或 tools\7zr.exe 后须重新 CMake 配置才会嵌入；本脚本在检测到该文件时会自动执行 cmake -S -B 再全量编译。
# -CMakeBuildDir：与 CMake -B 一致。默认 out\build；若你用 scripts\rebuild_gateway_static_release_qt5.bat 编静态版，应传 out\build_static_release_qt5，否则 cs_single.exe 仍来自另一构建树（可能无内嵌 UPDATE.exe）。

param(
    [string]$BuildConfig = "Release",
    [string]$QtPath = "",
    [string]$EnigmaVBoxPath = "D:\Program Files (x86)\Enigma Virtual Box\enigmavb.exe",
    [string]$OutputDir = "",       # 默认同「支付网关.exe」所在目录（编译后自动解析）
    [string]$CMakeBuildDir = ""   # 默认可传 out\build_static_release_qt5 等与 Qt5 静态脚本相同的 -B 目录
)

# 检查 Enigma Virtual Box 是否存在
if (-not (Test-Path $EnigmaVBoxPath)) {
    Write-Error "Enigma Virtual Box 未找到: $EnigmaVBoxPath"
    Write-Host "请从 https://enigmaprotector.com/en/files/enigmavb.html 下载并安装"
    exit 1
}

# 仓库根（本脚本在 scripts\ 下）
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($CMakeBuildDir)) {
    $BuildDir = Join-Path $RepoRoot 'out\build'
} elseif ([System.IO.Path]::IsPathRooted($CMakeBuildDir)) {
    $BuildDir = $CMakeBuildDir
} else {
    $BuildDir = Join-Path $RepoRoot $CMakeBuildDir
}
$userOutputDir = $OutputDir

# 设置 Qt 路径（如果未指定）：用于 windeployqt 与 cmake -DCMAKE_PREFIX_PATH
if ([string]::IsNullOrEmpty($QtPath)) {
    if ($env:CMAKE_PREFIX_PATH) {
        $first = ($env:CMAKE_PREFIX_PATH -split ';')[0].Trim()
        if ((Test-Path "$first\lib\cmake\Qt5\Qt5Config.cmake") -or (Test-Path "$first\lib\cmake\Qt6\Qt6Config.cmake")) {
            $QtPath = $first
        }
    }
    if ([string]::IsNullOrEmpty($QtPath) -and $env:Qt5_DIR) {
        $QtPath = Split-Path $env:Qt5_DIR -Parent
    }
    # 仅当未从 Qt5_DIR 得到路径时再使用 Qt6（避免两个环境变量都设了却编成 Qt6）
    if ([string]::IsNullOrEmpty($QtPath) -and $env:Qt6_DIR) {
        $QtPath = Split-Path $env:Qt6_DIR -Parent
    }
    if ([string]::IsNullOrEmpty($QtPath)) {
        $possiblePaths = @(
            "C:\Qt\5.15.2\msvc2019_64",
            "D:\Qt\6.11.0\msvc2022_64_static",
            "C:\Qt\6.5.0\msvc2019_64",
            "C:\Qt\6.6.0\msvc2019_64"
        )
        foreach ($path in $possiblePaths) {
            if (Test-Path "$path\bin\windeployqt.exe") {
                $QtPath = $path
                break
            }
        }
    }
}

if ([string]::IsNullOrEmpty($QtPath) -or -not (Test-Path "$QtPath\bin\windeployqt.exe")) {
    Write-Error "无法找到 Qt 路径，请设置 QtPath 参数或 Qt5_DIR/Qt6_DIR 环境变量"
    exit 1
}

$windeployqt = "$QtPath\bin\windeployqt.exe"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  单文件打包工具" -ForegroundColor Cyan
Write-Host "  仓库: $RepoRoot" -ForegroundColor DarkGray
Write-Host "  CMake -B: $BuildDir" -ForegroundColor DarkGray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$tool7za = Join-Path $RepoRoot "tools\7za.exe"
$tool7zr = Join-Path $RepoRoot "tools\7zr.exe"
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
$needCmakeConfigure = (Test-Path $tool7za) -or (Test-Path $tool7zr) -or (-not (Test-Path $cacheFile))
if ($needCmakeConfigure) {
    if ((Test-Path $tool7za) -or (Test-Path $tool7zr)) {
        Write-Host "检测到 tools\7za.exe / tools\7zr.exe 或尚未配置构建目录，正在运行 CMake 配置（含 -DCMAKE_PREFIX_PATH）..." -ForegroundColor Yellow
    } else {
        Write-Host "构建目录尚未配置，正在运行 CMake 配置（含 -DCMAKE_PREFIX_PATH）..." -ForegroundColor Yellow
    }
    & cmake -S $RepoRoot -B $BuildDir "-DCMAKE_PREFIX_PATH=$QtPath"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake 配置失败。请在仓库根手动执行（将 Qt 路径改成你的安装目录）:" -ForegroundColor Red
        Write-Host "  cmake -S . -B `"$BuildDir`" -DCMAKE_PREFIX_PATH=$QtPath"
        Write-Host "或设置环境变量后重试:"
        Write-Host "  set Qt6_DIR=D:\Qt\6.11.0\msvc2022_64_static\lib\cmake\Qt6"
        Write-Host "  cmake -S . -B out\build -DCMAKE_PREFIX_PATH=D:\Qt\6.11.0\msvc2022_64_static"
        exit 1
    }
    if ((Test-Path $tool7za) -or (Test-Path $tool7zr)) {
        Write-Host "CMake 配置完成。若首次嵌入解压工具，请确认上方有: Gateway: embedding tools/7za.exe 或 tools/7zr.exe" -ForegroundColor DarkGray
    }
} else {
    Write-Host "已存在 CMakeCache.txt 且未检测到需强制重新配置；跳过 cmake -S -B。若刚放入 tools\7za.exe / tools\7zr.exe、或需改 Qt 路径，请删除 $BuildDir\CMakeCache.txt 后重跑本脚本。" -ForegroundColor DarkGray
}

Write-Host "正在编译 $BuildConfig ..." -ForegroundColor Yellow
& cmake --build $BuildDir --config $BuildConfig
if ($LASTEXITCODE -ne 0) {
    Write-Error "构建失败"
    exit 1
}

$exeCandidates = @(
    (Join-Path $BuildDir "支付网关.exe"),
    (Join-Path (Join-Path $BuildDir $BuildConfig) "支付网关.exe")
)
$exePath = $null
foreach ($c in $exeCandidates) {
    if (Test-Path -LiteralPath $c) {
        $exePath = $c
        break
    }
}
if (-not $exePath) {
    Write-Error "未找到主程序 支付网关.exe（已尝试: $($exeCandidates -join '; ')）"
    exit 1
}

if ([string]::IsNullOrWhiteSpace($userOutputDir)) {
    $OutputDir = Split-Path $exePath -Parent
} elseif (-not [System.IO.Path]::IsPathRooted($userOutputDir)) {
    $OutputDir = Join-Path $RepoRoot $userOutputDir
} else {
    $OutputDir = $userOutputDir
}

$deployDir = Join-Path $OutputDir "qt_plugins"
$evbConfig = Join-Path $OutputDir "evb_config.xml"
$singleExe = Join-Path $OutputDir "cs_single.exe"

Write-Host "主程序: $exePath" -ForegroundColor DarkGray
Write-Host "输出目录: $OutputDir" -ForegroundColor DarkGray

Write-Host "1. 部署 Qt 依赖..." -ForegroundColor Green
if (Test-Path $deployDir) {
    Remove-Item $deployDir -Recurse -Force
}
New-Item -ItemType Directory -Path $deployDir -Force | Out-Null

& $windeployqt --release --no-compiler-runtime --no-translations --no-opengl-sw --dir $deployDir $exePath
if ($LASTEXITCODE -ne 0) {
    Write-Error "windeployqt 执行失败"
    exit 1
}

Write-Host "2. 生成 Enigma Virtual Box 配置..." -ForegroundColor Green
$evbXml = @"
<?xml version="1.0" encoding="utf-8"?>
<enigmavb>
    <output>$singleExe</output>
    <input>$exePath</input>
    <packfiles>
        <pack>
            <folder>$deployDir</folder>
            <subfolders>true</subfolders>
            <root>$OutputDir</root>
        </pack>
    </packfiles>
    <compress>true</compress>
    <compresslevel>9</compresslevel>
    <temppath>%TEMP%</temppath>
    <executionmode>0</executionmode>
</enigmavb>
"@
$evbXml | Out-File -FilePath $evbConfig -Encoding UTF8

Write-Host "3. 打包单文件..." -ForegroundColor Green
& $EnigmaVBoxPath pack $evbConfig
if ($LASTEXITCODE -ne 0) {
    Write-Error "Enigma Virtual Box 打包失败"
    exit 1
}

if (Test-Path $singleExe) {
    $originalSize = (Get-Item $exePath).Length
    $singleSize = (Get-Item $singleExe).Length
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  打包完成!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  原始文件: $exePath ($([math]::Round($originalSize/1MB, 2)) MB)"
    Write-Host "  单文件版: $singleExe ($([math]::Round($singleSize/1MB, 2)) MB)"
    Write-Host ""
} else {
    Write-Error "打包失败，未生成单文件"
    exit 1
}
