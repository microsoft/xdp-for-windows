<#

.SYNOPSIS
This script tests the xskrestricted eBPF sample by attaching an XSK redirect
eBPF program to a virtual XDPMP adapter, spawning a restricted child process,
and forwarding received frames back to the sender.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER TimeoutSeconds
    Duration of execution in seconds.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [Int32]$TimeoutSeconds = 10
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$XskRestricted = "$ArtifactsDir\test\xskrestricted.exe"
$BpfProgram = "$ArtifactsDir\test\bpf\xsk_redirect.sys"

# Verify binaries exist.
if (!(Test-Path $XskRestricted)) {
    Write-Error "$XskRestricted does not exist!"
}
if (!(Test-Path $BpfProgram)) {
    Write-Error "$BpfProgram does not exist!"
}

try {
    & "$RootDir\tools\log.ps1" -Start -Name sample_xskrestricted -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

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
    # Run xskrestricted with the xsk_redirect eBPF program.
    # The program spawns a restricted child, forwards traffic for TimeoutSeconds,
    # then exits.
    #
    Write-Verbose "$XskRestricted $IfIndex -BpfProgram $BpfProgram -ProgramName xsk_redirect -TimeoutSeconds $TimeoutSeconds"
    & $XskRestricted $IfIndex -BpfProgram $BpfProgram -ProgramName xsk_redirect -TimeoutSeconds $TimeoutSeconds

    if ($LastExitCode -ne 0) {
        Write-Error "xskrestricted failed with exit code $LastExitCode"
    }

    Write-Output "xskrestricted sample test PASSED"

} finally {
    & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name sample_xskrestricted -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
