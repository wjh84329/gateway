# 同一 URL + Scan 参数：C++ qr_mir_dump 与 C# QRCoder（老网关 QrCoderUtil URL 分支）输出应逐字节一致。
# 在 gateway 仓库根目录执行:
#   .\tools\qr_mir_parity\VerifyQrMirParity.ps1
#   .\tools\qr_mir_parity\VerifyQrMirParity.ps1 -Url "https://..." -Serial 4 -ResourceCode 29 -ImageCode 44
# 可选: -BuildDir out\build_static_release_qt5

param(
    [string]$Url = "https://api.feixpay.cn/Call/Hlbbpay/wxRawPub?pay_order_id=202605120050421912",
    [int]$Serial = 4,
    [string]$ResourceCode = "29",
    [string]$ImageCode = "44",
    [int]$XOffset = 10,
    [int]$YOffset = 10,
    [string]$BuildDir = "out\build_static_release_qt5"
)

$ErrorActionPreference = "Stop"
$GatewayRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$CppExe = Join-Path $GatewayRoot "$BuildDir\qr_mir_dump.exe"
$CsProj = Join-Path $PSScriptRoot "reference\QrMirParityReference.csproj"
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("qr_mir_parity_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $Work | Out-Null
$OutCpp = Join-Path $Work "cpp.txt"
$OutCs = Join-Path $Work "cs.txt"

if (-not (Test-Path -LiteralPath $CppExe)) {
    Write-Error "找不到 $CppExe 。请先在该 BuildDir 下编译目标 qr_mir_dump（例如 cmake --build ... --target qr_mir_dump）。"
}

$argsCommon = @(
    $Url,
    $Serial.ToString(),
    $ResourceCode,
    $ImageCode,
    $XOffset.ToString(),
    $YOffset.ToString()
)

& $CppExe @($argsCommon + @($OutCpp))
if ($LASTEXITCODE -ne 0) { throw "qr_mir_dump 退出码 $LASTEXITCODE" }

dotnet build $CsProj -c Release -v q | Out-Null
if ($LASTEXITCODE -ne 0) { throw "dotnet build 失败" }

dotnet run -c Release --project $CsProj --no-build -- @($argsCommon + @($OutCs)) | Out-Null
if ($LASTEXITCODE -ne 0) { throw "dotnet run 失败" }

$h1 = (Get-FileHash -LiteralPath $OutCpp -Algorithm SHA256).Hash
$h2 = (Get-FileHash -LiteralPath $OutCs -Algorithm SHA256).Hash
if ($h1 -eq $h2) {
    Write-Host "OK 一致 SHA256=$h1"
    Remove-Item -LiteralPath $Work -Recurse -Force
    exit 0
}

Write-Host "不一致。临时目录: $Work"
& fc.exe /b $OutCpp $OutCs
exit 1
