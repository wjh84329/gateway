#requires -Version 7.0
<#
.SYNOPSIS
  从网关程序根目录下的「模板」文件夹生成 InstallScriptTemplateDefaults 的 INSERT SQL（UTF-8 输出）。

.DESCRIPTION
  必须在 PowerShell 7+ (pwsh) 下运行，避免 Windows PowerShell 5.x 与旧版 .NET 在编码上的差异。
  读取规则：优先识别 UTF-8 BOM / UTF-16 LE BOM；否则按简体中文环境常用代码页 GBK(936) 解码（与网关 ReadExistingTextFile 本地字节语义更接近）。

.PARAMETER TemplateRoot
  「模板」目录绝对路径。默认：本脚本所在目录的上一级下的「模板」文件夹。

.PARAMETER OutFile
  生成的 .sql 路径。默认：脚本目录下 install_script_template_seed.generated.sql

.PARAMETER SkipRootTxt
  不导入「模板」根目录下的 .txt（默认会导入，EngineFolder 为空串，与网关先子目录再根目录回退一致）。

.PARAMETER InstallOnly
  仅包含老网关 ProcessInstall.cs 安装分区时实际读取的模板文件（排除打款/通区编辑用/设计器用等磁盘冗余文件）。
#>
[CmdletBinding()]
param(
    [Parameter()]
    [string] $TemplateRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) '模板'),

    [Parameter()]
    [string] $OutFile = (Join-Path $PSScriptRoot 'install_script_template_seed.generated.sql'),

    [Parameter()]
    [switch] $SkipRootTxt,

    [Parameter()]
    [switch] $InstallOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Initialize-GbkEncoding {
    try {
        Add-Type -AssemblyName 'System.Text.Encoding.CodePages' -ErrorAction Stop | Out-Null
        [void][System.Text.Encoding]::RegisterProvider([System.Text.CodePagesEncodingProvider]::Instance)
        return [System.Text.Encoding]::GetEncoding(936)
    } catch {
        Write-Warning "无法注册 CodePages，非 UTF 文件将尝试使用系统默认编码: $($_.Exception.Message)"
        return [System.Text.Encoding]::Default
    }
}

function Read-TemplateFileText {
    param([Parameter(Mandatory)][string] $Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0) { return '' }

    # UTF-8 BOM
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        return [System.Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
    }
    # UTF-16 LE BOM
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        return [System.Text.UnicodeEncoding]::new($false, $true).GetString($bytes, 2, $bytes.Length - 2)
    }
    # UTF-16 BE BOM
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        return [System.Text.UnicodeEncoding]::new($true, $true).GetString($bytes, 2, $bytes.Length - 2)
    }

    $gbk = Initialize-GbkEncoding
    return $gbk.GetString($bytes)
}

function Escape-SqlNString {
    param([string] $s)
    if ($null -eq $s) { return "N''" }
    return "N'" + ($s -replace "'", "''") + "'"
}

if (-not (Test-Path -LiteralPath $TemplateRoot -PathType Container)) {
    throw "模板目录不存在: $TemplateRoot （请用 -TemplateRoot 指定网关根目录下的「模板」）"
}

$rows = [System.Collections.Generic.List[object]]::new()
$root = [System.IO.Path]::GetFullPath($TemplateRoot)

# 子目录：EngineFolder = 目录名
Get-ChildItem -LiteralPath $root -Directory -Force | ForEach-Object {
    $engineFolder = $_.Name
    Get-ChildItem -LiteralPath $_.FullName -File -Force -Filter '*.txt' | ForEach-Object {
        $text = Read-TemplateFileText -Path $_.FullName
        $rows.Add([pscustomobject]@{
                EngineFolder = $engineFolder
                FileName     = $_.Name
                Content      = $text
            })
    }
}

# 根目录 .txt（默认导入；EngineFolder 空串 = 全引擎共用回退）
if (-not $SkipRootTxt) {
    Get-ChildItem -LiteralPath $root -File -Force -Filter '*.txt' | ForEach-Object {
        $text = Read-TemplateFileText -Path $_.FullName
        $rows.Add([pscustomobject]@{
                EngineFolder = ''
                FileName     = $_.Name
                Content      = $text
            })
    }
}

if ($rows.Count -eq 0) {
    throw "未在「模板」下找到任何 .txt。请确认目录正确，或去掉 -SkipRootTxt 以包含根目录文件。"
}

# 与老网关 Gateway\Common\ProcessInstall.cs 中 File.ReadAllText(模板\...) 路径一致
if ($InstallOnly) {
    $subdirAllow = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    @('NPC.txt', '微信密保.txt', '自助转区.txt', '密保登陆.txt', '充值.txt', '通区测试充值.txt') | ForEach-Object { [void]$subdirAllow.Add($_) }
    $rootAllow = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    @('转区点.txt', '测试领取.txt', '附加赠送.txt', '装备.txt', '积分.txt', '充值.txt', '通区测试充值.txt') | ForEach-Object { [void]$rootAllow.Add($_) }

    $filtered = [System.Collections.Generic.List[object]]::new()
    foreach ($r in $rows) {
        if ([string]::IsNullOrEmpty($r.EngineFolder)) {
            if ($rootAllow.Contains($r.FileName)) { $filtered.Add($r) }
        } else {
            if ($subdirAllow.Contains($r.FileName)) { $filtered.Add($r) }
        }
    }
    $rows = $filtered
}

if ($rows.Count -eq 0) {
    throw "过滤后无可用行。若使用 -InstallOnly，请确认模板目录下存在安装所需文件。"
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine('-- InstallScriptTemplateDefaults 种子（与老网关 ProcessInstall 安装脚本读取的模板文件一致）')
if ($InstallOnly) { [void]$sb.AppendLine('-- 已使用 -InstallOnly 排除非安装路径文件') }
[void]$sb.AppendLine('-- 源目录: ' + $root.Replace("'", "''"))
[void]$sb.AppendLine('SET NOCOUNT ON;')
[void]$sb.AppendLine('BEGIN TRANSACTION;')
[void]$sb.AppendLine('DELETE FROM [dbo].[InstallScriptTemplateDefaults];')
[void]$sb.AppendLine()

foreach ($r in $rows) {
    $ef = Escape-SqlNString $r.EngineFolder
    $fn = Escape-SqlNString $r.FileName
    $ct = Escape-SqlNString $r.Content
    [void]$sb.AppendLine("INSERT INTO [dbo].[InstallScriptTemplateDefaults] ([EngineFolder], [FileName], [Content], [UpdatedAt]) VALUES ($ef, $fn, $ct, SYSUTCDATETIME());")
}

[void]$sb.AppendLine()
[void]$sb.AppendLine('COMMIT TRANSACTION;')

[System.IO.File]::WriteAllText($OutFile, $sb.ToString(), $utf8NoBom)
Write-Host "已写入 $($rows.Count) 条 INSERT -> $OutFile"
