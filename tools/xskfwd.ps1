<#

.SYNOPSIS
Runs the xskfwd sample, which forwards received frames back to the sender.

By default the eBPF-based sample (xskfwd.exe) is run. Pass -Deprecated to run
the legacy built-in rules-based sample (xskfwd-deprecated.exe) instead.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER Duration
    Duration of the test in seconds.

.PARAMETER Deprecated
    Run the legacy built-in rules-based sample instead of the eBPF sample.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [Int32]$Duration = 10,

    [Parameter(Mandatory = $false)]
    [switch]$Deprecated
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform

if ($Deprecated) {
    #
    # The deprecated sample uses the built-in rules-based program API, which is
    # deprecated and planned for removal.
    #
    Write-Warning "xskfwd -Deprecated uses the built-in program API (xskfwd-deprecated); this path is deprecated and planned for removal."
    $XskFwd = "$ArtifactsDir\test\xskfwd-deprecated.exe"
} else {
    $XskFwd = "$ArtifactsDir\test\xskfwd.exe"
    $BpfProgram = "$ArtifactsDir\test\bpf\xsk_redirect.sys"
}

# Verify binaries exist.
if (!(Test-Path $XskFwd)) {
    Write-Error "$XskFwd does not exist!"
}
if (!$Deprecated -and !(Test-Path $BpfProgram)) {
    Write-Error "$BpfProgram does not exist!"
}

$XskFwdProcess = $null

try {
    & "$RootDir\tools\log.ps1" -Start -Name xskfwd -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

    if (!$Deprecated) {
        Write-Verbose "installing ebpf..."
        & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Platform $Platform
        Write-Verbose "installed ebpf."
    }

    Write-Verbose "installing xdp..."
    if ($Deprecated) {
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform
    } else {
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform -EnableEbpf
    }
    Write-Verbose "installed xdp."

    Write-Verbose "installing fndis..."
    & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Platform $Platform
    Write-Verbose "installed fndis."

    Write-Verbose "installing xdpmp..."
    & "$RootDir\tools\setup.ps1" -Install xdpmp -Config $Config -Platform $Platform -XdpmpPollProvider FNDIS
    Write-Verbose "installed xdpmp."

    $IfIndex = (Get-NetAdapter XDPMP).ifIndex

    $ArgList = @("$IfIndex")
    if (!$Deprecated) {
        $ArgList += @("-BpfProgram", $BpfProgram, "-ProgramName", "xsk_redirect")
    }

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
    if (!$Deprecated) {
        & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -Force -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name xskfwd -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
