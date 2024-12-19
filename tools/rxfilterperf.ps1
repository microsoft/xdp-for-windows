param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("System", "Generic", "Native")]
    [string]$XdpMode = "System",

    [Parameter(Mandatory = $false)]
    [int]$QueueCount = 1,

    [Parameter(Mandatory = $false)]
    [int]$Timeout = 30,

    [Parameter(Mandatory=$false)]
    [switch]$Fndis = $false,

    [Parameter(Mandatory = $false)]
    [string]$Action = "Drop"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$LogsDir = "$RootDir\artifacts\logs"
$RxFilter = "$ArtifactsDir\test\rxfilter.exe"

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null
$RxFilterProcess = $null

$XdpmpPollProvider = "NDIS"
if ($Fndis) {
    $XdpmpPollProvider = "FNDIS"
}

try {
    if ($Fndis) {
        Write-Verbose "installing fndis..."
        & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Platform $Platform
        Write-Verbose "installed fndis."
    }

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform
    Write-Verbose "installed xdp."

    Write-Verbose "installing xdpmp..."
    & "$RootDir\tools\setup.ps1" -Install xdpmp -XdpmpPollProvider $XdpmpPollProvider -Config $Config -Platform $Platform
    Write-Verbose "installed xdpmp."

    Write-Verbose "Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount"
    Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount

    & "$RootDir\tools\log.ps1" -Start -Name rxfiltercpu -Profile CpuCswitchSample.Verbose -Config $Config -Platform $Platform

    $ArgList = `
        "-IfIndex", (Get-NetAdapter -Name XDPMP).ifIndex, `
        "-QueueId", "*", "-MatchType" ,"All", "-Action", $Action, `
        "-XdpMode", $XdpMode
    Write-Verbose "$RxFilter $ArgList"
    $RxFilterProcess = Start-Process $RxFilter -PassThru -ArgumentList $ArgList

    & $RootDir\tools\xdpmpratesim.ps1 -AdapterName XDPMP -Unlimited

    Write-Verbose "Start XDP counters:"
    Get-Counter ((Get-Counter -ListSet XDP*).Paths) -ErrorAction Ignore | Out-String | Write-Verbose

    $StartPackets = (Get-NetAdapterStatistics -Name XDPMP).ReceivedUnicastPackets
    Start-Sleep -Seconds $Timeout
    $EndPackets = (Get-NetAdapterStatistics -Name XDPMP).ReceivedUnicastPackets

    Write-Verbose "End XDP counters:"
    Get-Counter ((Get-Counter -ListSet XDP*).Paths) -ErrorAction Ignore | Out-String | Write-Verbose

    & $RootDir\tools\xdpmpratesim.ps1 -AdapterName XDPMP -RxFramesPerInterval 1000

    Write-Output "Filtered $(($EndPackets - $StartPackets)) packets in $Timeout seconds ($(($EndPackets - $StartPackets) / 1000 / $Timeout) Kpps)."
} finally {
    if ($RxFilterProcess) {
        Stop-Process -InputObject $RxFilterProcess
    }

    & "$RootDir\tools\log.ps1" -Stop -Name rxfiltercpu -Config $Config -Platform $Platform -ErrorAction 'Continue'

    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    if ($Fndis) {
        & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
}
