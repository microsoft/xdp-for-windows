<#

.SYNOPSIS
This script tests the rxfilter eBPF sample by attaching a drop-all eBPF
program to a virtual XDPMP adapter and verifying traffic is processed.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER Duration
    Duration of the test in seconds.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [Int32]$Duration = 10
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$RxFilter = "$ArtifactsDir\test\rxfilter.exe"
$BpfProgram = "$ArtifactsDir\test\bpf\drop.sys"

# Verify binaries exist.
if (!(Test-Path $RxFilter)) {
    Write-Error "$RxFilter does not exist!"
}
if (!(Test-Path $BpfProgram)) {
    Write-Error "$BpfProgram does not exist!"
}

$RxFilterProcess = $null

try {
    & "$RootDir\tools\log.ps1" -Start -Name sample_rxfilter -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform
    Write-Verbose "installed xdp."

    Write-Verbose "installing fndis..."
    & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Platform $Platform
    Write-Verbose "installed fndis."

    Write-Verbose "installing xdpmp..."
    & "$RootDir\tools\setup.ps1" -Install xdpmp -Config $Config -Platform $Platform -XdpmpPollProvider FNDIS
    Write-Verbose "installed xdpmp."

    Write-Verbose "installing ebpf..."
    & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Platform $Platform
    Write-Verbose "installed ebpf."

    $IfIndex = (Get-NetAdapter XDPMP).ifIndex

    #
    # Start rxfilter with the drop-all eBPF program.
    #
    $ArgList = "-IfIndex", $IfIndex, "-BpfProgram", $BpfProgram, "-ProgramName", "drop"
    Write-Verbose "$RxFilter $ArgList"
    $RxFilterProcess = Start-Process $RxFilter -PassThru -ArgumentList $ArgList

    $StartPackets = (Get-NetAdapterStatistics -Name XDPMP).ReceivedUnicastPackets
    Write-Verbose "Waiting $Duration seconds with traffic flowing..."
    Start-Sleep -Seconds $Duration
    $EndPackets = (Get-NetAdapterStatistics -Name XDPMP).ReceivedUnicastPackets

    & $RootDir\tools\xdpmpratesim.ps1 -AdapterName XDPMP -RxFramesPerInterval 1000

    $PacketsProcessed = $EndPackets - $StartPackets
    Write-Output "rxfilter processed $PacketsProcessed packets in $Duration seconds."

    if ($RxFilterProcess.HasExited) {
        Write-Error "rxfilter exited unexpectedly with code $($RxFilterProcess.ExitCode)"
    }

    if ($PacketsProcessed -eq 0) {
        Write-Error "No packets were processed - eBPF program may not have attached correctly"
    }

    Write-Output "rxfilter sample test PASSED"

} finally {
    if ($null -ne $RxFilterProcess -and !$RxFilterProcess.HasExited) {
        Stop-Process -Force -InputObject $RxFilterProcess -ErrorAction 'Continue'
    }
    & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name sample_rxfilter -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
