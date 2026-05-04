# PowerShell 脚本：构建单文件可执行程序
# 使用 windeployqt + Enigma Virtual Box

param(
    [string]$BuildConfig = "Release",
    [string]$QtPath = "",
    [string]$EnigmaVBoxPath = "D:\Program Files (x86)\Enigma Virtual Box\enigmavb.exe",
    [string]$OutputDir = "out\build\$BuildConfig"
)

# 检查 Enigma Virtual Box 是否存在
if (-not (Test-Path $EnigmaVBoxPath)) {
    Write-Error "Enigma Virtual Box 未找到: $EnigmaVBoxPath"
    Write-Host "请从 https://enigmaprotector.com/en/files/enigmavb.html 下载并安装"
    exit 1
}

# 设置 Qt 路径（如果未指定）
if ([string]::IsNullOrEmpty($QtPath)) {
    # 尝试从环境变量获取
    if ($env:Qt5_DIR) {
        $QtPath = Split-Path $env:Qt5_DIR -Parent
    } elseif ($env:Qt6_DIR) {
        $QtPath = Split-Path $env:Qt6_DIR -Parent
    } else {
        # 常见 Qt 安装路径
        $possiblePaths = @(
            "C:\Qt\5.15.2\msvc2019_64",
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
$exePath = "$OutputDir\cs.exe"
$deployDir = "$OutputDir\qt_plugins"
$evbConfig = "$OutputDir\evb_config.xml"
$singleExe = "$OutputDir\cs_single.exe"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  单文件打包工具" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查主程序是否存在
if (-not (Test-Path $exePath)) {
    Write-Host "正在构建项目..." -ForegroundColor Yellow
    cmake --build out\build --config $BuildConfig
    if ($LASTEXITCODE -ne 0) {
        Write-Error "构建失败"
        exit 1
    }
}

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