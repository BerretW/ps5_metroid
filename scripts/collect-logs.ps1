#requires -Version 5.1
param(
    [string]$Path = "logs\project_echo.log",
    [int]$Tail = 200
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$logPath = Join-Path $root $Path
if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
    throw "Log file not found: $logPath"
}
Get-Content -LiteralPath $logPath -Tail $Tail
