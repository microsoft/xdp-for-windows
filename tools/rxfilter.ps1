param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [string]$AdapterName = "XDPMP",

    [Parameter(Mandatory = $false)]
    [ValidateSet("System", "Generic", "Native")]
    [string]$XdpMode = "System",

    [Parameter(Mandatory = $false)]
    [int]$QueueCount = 1,

    [Parameter(Mandatory = $false)]
    [string]$Action = "Drop"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform

for ($i = 0; $i -lt $QueueCount; $i++) {
    Start-Process $ArtifactsDir\test\rxfilter.exe -ArgumentList `
        "-IfIndex", (Get-NetAdapter -Name $AdapterName).ifIndex, `
        "-QueueId", $i, "-MatchType" ,"All", "-Action", $Action, `
        "-XdpMode", $XdpMode
}
