<#

.SYNOPSIS
This script runs spinxsk.exe over and over until the number of specified minutes
have elapsed or a failure is seen. The tool is run multiple times in short iterations
rather than for one long iteration to keep the size of traces down and to provide
more coverage for setup and cleanup.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER Minutes
    Duration of execution in minutes. If 0, runs until ctrl+c is pressed.

.PARAMETER Stats
    Periodic socket statistics output.

.PARAMETER QueueCount
    Number of queues to spin.

.Parameter FuzzerCount
    Number of fuzzer threads per queue.

.Parameter SuccessThresholdPercent
    Minimum socket success rate, percent.

.PARAMETER CleanDatapath
    Avoid actions that invalidate the datapath.

.PARAMETER NoLogs
    Do not capture logs.

.PARAMETER XdpmpPollProvider
    Poll provider for XDPMP.

.PARAMETER EnableEbpf
    Enable eBPF in the XDP driver and spinxsk test cases.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [Int32]$Minutes = 0,

    [Parameter(Mandatory = $false)]
    [switch]$Stats = $false,

    [Parameter(Mandatory = $false)]
    [Int32]$QueueCount = 2,

    [Parameter(Mandatory = $false)]
    [Int32]$FuzzerCount = 0,

    [Parameter(Mandatory = $false)]
    [Int32]$SuccessThresholdPercent = -1,

    [Parameter(Mandatory = $false)]
    [switch]$CleanDatapath = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoLogs = $false,

    [Parameter(Mandatory = $false)]
    [switch]$BreakOnWatchdog = $false,

    [Parameter(Mandatory = $false)]
    [ValidateSet("NDIS", "FNDIS")]
    [string]$XdpmpPollProvider = "NDIS",

    [Parameter(Mandatory = $false)]
    [switch]$EnableEbpf = $false,

    [Parameter(Mandatory = $false)]
    [switch]$EbpfPreinstalled = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$LogsDir = "$RootDir\artifacts\logs"
$SpinXsk = "$ArtifactsDir\spinxsk.exe"
$LiveKD = Get-CoreNetCiArtifactPath -Name "livekd64.exe"
$KD = Get-CoreNetCiArtifactPath -Name "kd.exe"

# Verify all the files are present.
if (!(Test-Path $SpinXsk)) {
    Write-Error "$SpinXsk does not exist!"
}

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

$StartTime = Get-Date

while (($Minutes -eq 0) -or (((Get-Date)-$StartTime).TotalMinutes -lt $Minutes)) {

    $ThisIterationMinutes = 5
    if ($Minutes -ne 0) {
        $TotalRemainingMinutes = [math]::max(1, [math]::ceiling($Minutes - ((Get-Date)-$StartTime).TotalMinutes))
        if ($ThisIterationMinutes -gt $TotalRemainingMinutes) {
            $ThisIterationMinutes = $TotalRemainingMinutes
        }
    }

    try {
        if (!$NoLogs) {
            & "$RootDir\tools\log.ps1" -Start -Name spinxsk -Profile SpinXsk.Verbose -Config $Config -Arch $Arch
            & "$RootDir\tools\log.ps1" -Start -Name spinxskebpf -Profile SpinXskEbpf.Verbose -LogMode Memory -Config $Config -Arch $Arch
            & "$RootDir\tools\log.ps1" -Start -Name spinxskcpu -Profile CpuCswitchSample.Verbose -Config $Config -Arch $Arch
        }
        if ($XdpmpPollProvider -eq "FNDIS") {
            Write-Verbose "installing fndis..."
            & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Arch $Arch
            Write-Verbose "installed fndis."
        }

        Write-Verbose "installing xdp..."
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Arch $Arch -EnableEbpf:$EnableEbpf
        Write-Verbose "installed xdp."

        Write-Verbose "installing xdpmp..."
        & "$RootDir\tools\setup.ps1" -Install xdpmp -XdpmpPollProvider $XdpmpPollProvider -Config $Config -Arch $Arch
        Write-Verbose "installed xdpmp."

        if (!$EbpfPreinstalled) {
            Write-Verbose "installing ebpf..."
            & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Arch $Arch
            Write-Verbose "installed ebpf."
        }

        Write-Verbose "Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount"
        Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount

        Write-Verbose "reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpFaultInject /d 1 /t REG_DWORD /f"
        reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpFaultInject /d 1 /t REG_DWORD /f | Write-Verbose

        $Args = `
            "-IfIndex", (Get-NetAdapter XDPMP).ifIndex, `
            "-QueueCount", $QueueCount, `
            "-Minutes", $ThisIterationMinutes
        if ($Stats) {
            $Args += "-Stats"
        }
        if ($FuzzerCount -ne 0) {
            $Args += "-FuzzerCount", $FuzzerCount
        }
        if ($CleanDatapath) {
            $Args += "-CleanDatapath"
        }
        if ($SuccessThresholdPercent -ge 0) {
            $Args += "-SuccessThresholdPercent", $SuccessThresholdPercent
        }
        if ($BreakOnWatchdog) {
            $Args += "-WatchdogCmd", "break"
        } else {
            $Args += "-WatchdogCmd", "$LiveKD -o $LogsDir\spinxsk_watchdog.dmp -k $KD -ml -accepteula"
        }
        if ($EnableEbpf) {
            $Args += "-EnableEbpf"
        }
        Write-Verbose "$SpinXsk $Args"
        & $SpinXsk $Args
        if ($LastExitCode -ne 0) {
            throw "SpinXsk failed with $LastExitCode"
        }
    } finally {
        if (!$EbpfPreinstalled) {
            & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        }
        & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        if ($XdpmpPollProvider -eq "FNDIS") {
            & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Arch $Arch -ErrorAction 'Continue'
        }
        if (!$NoLogs) {
            & "$RootDir\tools\log.ps1" -Stop -Name spinxskcpu -Config $Config -Arch $Arch -ErrorAction 'Continue'
            & "$RootDir\tools\log.ps1" -Stop -Name spinxskebpf -Config $Config -Arch $Arch -ErrorAction 'Continue'
            & "$RootDir\tools\log.ps1" -Stop -Name spinxsk -Config $Config -Arch $Arch -ErrorAction 'Continue'
        }
    }
}
