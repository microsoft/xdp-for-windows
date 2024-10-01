param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [int]$Minutes = 0,

    [Parameter(Mandatory = $false)]
    [int]$Workers = 1
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$LogsDir = "$RootDir\artifacts\logs"

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

$Options = @()

if ($Minutes -gt 0) {
    $Options += "-max_total_time=$($Minutes * 60)"
}

if ($Workers -gt 1) {
    $Options += "-jobs=$Workers"
    $Options += "-workers=$Workers"
}

try {
    Push-Location $LogsDir
    $env:ASAN_SAVE_DUMPS="$pwd\asan.dmp"

    Write-Verbose "$ArtifactsDir\test\pktfuzz.exe $Options"
    & $ArtifactsDir\test\pktfuzz.exe $Options

    if (!$?) {
        Write-Error "pktfuzz.exe failed: $LastExitCode"
    }
} catch {
    Write-Error "pktfuzz failed: $($_ | Out-String)"
} finally {
    Pop-Location
}
