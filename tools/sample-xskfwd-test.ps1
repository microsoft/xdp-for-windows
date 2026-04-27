<#

.SYNOPSIS
This script tests the xskfwd eBPF sample by attaching an XSK redirect eBPF
program to a virtual XDPMP adapter, generating traffic, and forwarding
received frames back to the sender.

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
$XskFwd = "$ArtifactsDir\test\xskfwd.exe"
$BpfProgram = "$ArtifactsDir\test\bpf\xsk_redirect.sys"

# Verify binaries exist.
if (!(Test-Path $XskFwd)) {
    Write-Error "$XskFwd does not exist!"
}
if (!(Test-Path $BpfProgram)) {
    Write-Error "$BpfProgram does not exist!"
}

$XskFwdProcess = $null

try {
    & "$RootDir\tools\log.ps1" -Start -Name sample_xskfwd -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

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
    # Start xskfwd with the xsk_redirect eBPF program.
    #
    $ArgList = "$IfIndex", "-BpfProgram", $BpfProgram, "-ProgramName", "xsk_redirect"
    Write-Verbose "$XskFwd $ArgList"
    $XskFwdProcess = Start-Process $XskFwd -PassThru -ArgumentList $ArgList

    Write-Verbose "Waiting $Duration seconds with traffic flowing..."
    Start-Sleep -Seconds $Duration

    & $RootDir\tools\xdpmpratesim.ps1 -AdapterName XDPMP -RxFramesPerInterval 1000

    if ($XskFwdProcess.HasExited) {
        Write-Error "xskfwd exited unexpectedly with code $($XskFwdProcess.ExitCode)"
    }

    Write-Output "xskfwd sample test PASSED"

} finally {
    if ($null -ne $XskFwdProcess -and !$XskFwdProcess.HasExited) {
        Stop-Process -Force -InputObject $XskFwdProcess -ErrorAction 'Continue'
    }
    & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name sample_xskfwd -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
