#!/usr/bin/env pwsh
<#
.SYNOPSIS
  仅用 dotnet 生成老网关同款 Mir 文本（QRCoder 1.6.0，与 QrCoderUtil.GenerateAndSaveQrCodeAsync1 URL 分支一致），无需编译 Qt。

.EXAMPLE
  # 默认：你的充值图片目录下的 2629_testplayer003.txt
  .\LegacyMirFromUrl.ps1

.EXAMPLE
  .\LegacyMirFromUrl.ps1 -ExpectedFile 'D:\平台验证\充值二维码\充值图片\2629_testplayer003.txt' -OutFile 'D:\temp\gen.txt'
#>
param(
    [string]$Url = 'https://api.feixpay.cn/Call/Hlbbpay/wxRawPub?pay_order_id=202605120050421912',
    [int]$Serial = 4,
    [string]$ResourceCode = '29',
    [string]$ImageCode = '44',
    [int]$XOffset = 10,
    [int]$YOffset = 10,
    [string]$OutFile = '',
    [string]$ExpectedDir = 'D:\平台验证\充值二维码\充值图片',
    [string]$ExpectedName = '2629_testplayer003.txt',
    [string]$ExpectedFile = '',
    [switch]$SkipCompare
)

$ErrorActionPreference = 'Stop'
$CsProj = Join-Path $PSScriptRoot 'reference' 'QrMirParityReference.csproj'
if (-not (Test-Path -LiteralPath $CsProj)) {
    Write-Error "找不到 $CsProj"
}

if (-not $OutFile) {
    $OutFile = Join-Path ([System.IO.Path]::GetTempPath()) ('legacy_mir_' + [Guid]::NewGuid().ToString('N') + '.txt')
}

if (-not $ExpectedFile) {
    $ExpectedFile = Join-Path $ExpectedDir $ExpectedName
}

$argsApp = @(
    $Url,
    "$Serial",
    $ResourceCode,
    $ImageCode,
    "$XOffset",
    "$YOffset",
    $OutFile
)

Push-Location (Split-Path -Parent $CsProj)
try {
    dotnet build (Split-Path -Leaf $CsProj) -c Release -v q | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "dotnet build 失败 (exit $LASTEXITCODE)" }
    dotnet run -c Release --project (Split-Path -Leaf $CsProj) --no-build -- @argsApp | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "dotnet run 失败 (exit $LASTEXITCODE)" }
}
finally {
    Pop-Location
}

$len = (Get-Item -LiteralPath $OutFile).Length
Write-Host "已生成: $OutFile ($len 字节)"

if ($SkipCompare) {
    exit 0
}

if (-not (Test-Path -LiteralPath $ExpectedFile)) {
    Write-Warning "对比文件不存在，跳过对比: $ExpectedFile"
    Write-Host "可将 -ExpectedDir / -ExpectedName 改成实际文件名，或把本机 txt 完整路径传给 -ExpectedFile。"
    exit 0
}

$gen = [System.IO.File]::ReadAllBytes($OutFile)
$exp = [System.IO.File]::ReadAllBytes($ExpectedFile)
$equal = $true
if ($gen.Length -ne $exp.Length) {
    $equal = $false
}
else {
    for ($i = 0; $i -lt $gen.Length; $i++) {
        if ($gen[$i] -ne $exp[$i]) {
            $equal = $false
            Write-Host "首处差异偏移=$i 生成=0x$('{0:X2}' -f $gen[$i]) 期望=0x$('{0:X2}' -f $exp[$i])"
            break
        }
    }
}

if ($equal) {
    Write-Host "OK 与 `"$ExpectedFile`" 逐字节一致"
    exit 0
}

Write-Host "FAIL 与 `"$ExpectedFile`" 不一致 (生成 $($gen.Length) 字节, 期望 $($exp.Length) 字节)"
Write-Host "二进制对比: fc.exe /b `"$OutFile`" `"$ExpectedFile`""
exit 1
