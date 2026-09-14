<#

.SYNOPSIS
Measures the effective XDP fault-injection rate on a simple, interface-independent
primitive (XSK create + UMEM registration) via xdpfaultrate.exe. It installs XDP,
enables XDP's fault injection (XdpFaultInject), runs the probe, and reports the
per-attempt success rate. Intended to run on the spinxsk pool, where driver
verifier low-resources simulation is already active, so the reported rate reflects
the same injection spinxsk experiences.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER Iterations
    Number of probe iterations to run.

.PARAMETER EbpfPreinstalled
    eBPF is already installed (inbox); do not install/uninstall it here.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [Int32]$Iterations = 10000,

    [Parameter(Mandatory = $false)]
    [switch]$EbpfPreinstalled = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$FaultRate = "$ArtifactsDir\test\xdpfaultrate.exe"

if (!(Test-Path $FaultRate)) {
    Write-Error "$FaultRate does not exist!"
}

try {
    if (!$EbpfPreinstalled) {
        Write-Verbose "installing ebpf..."
        & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Platform $Platform
    }

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform -EnableEbpf

    # Enable XDP's internal fault injection, matching spinxsk.
    Write-Verbose "reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpFaultInject /d 1 /t REG_DWORD /f"
    reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpFaultInject /d 1 /t REG_DWORD /f | Write-Verbose

    Write-Host "=== xdpfaultrate (Config=$Config Platform=$Platform Iterations=$Iterations) ==="
    & $FaultRate -Iterations $Iterations
    if ($LastExitCode -ne 0) {
        throw "xdpfaultrate failed with $LastExitCode"
    }
} finally {
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    if (!$EbpfPreinstalled) {
        & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
}
