<#

.SYNOPSIS
Runs the xskrestricted sample, which attaches an XSK redirect program, spawns a
restricted child process, and forwards received frames back to the sender.

By default the eBPF-based sample (xskrestricted.exe) is run. Pass -Deprecated to
run the legacy built-in rules-based sample (xskrestricted-deprecated.exe).

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER TimeoutSeconds
    Duration of execution in seconds. Default is 10.

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
    [Int32]$TimeoutSeconds = 10,

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
    Write-Warning "xskrestricted -Deprecated uses the built-in program API (xskrestricted-deprecated); this path is deprecated and planned for removal."
    $XskRestricted = "$ArtifactsDir\test\xskrestricted-deprecated.exe"
} else {
    $XskRestricted = "$ArtifactsDir\test\xskrestricted.exe"
    $BpfProgram = "$ArtifactsDir\test\bpf\xsk_redirect.sys"
}

# Verify binaries exist.
if (!(Test-Path $XskRestricted)) {
    Write-Error "$XskRestricted does not exist!"
}
if (!$Deprecated -and !(Test-Path $BpfProgram)) {
    Write-Error "$BpfProgram does not exist!"
}

try {
    & "$RootDir\tools\log.ps1" -Start -Name xskrestricted -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

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

    #
    # Run xskrestricted. The program spawns a restricted child, forwards traffic
    # for TimeoutSeconds, then exits.
    #
    $ArgList = @("$IfIndex", "-TimeoutSeconds", "$TimeoutSeconds")
    if (!$Deprecated) {
        $ArgList += @("-BpfProgram", $BpfProgram, "-ProgramName", "xsk_redirect")
    }

    Write-Verbose "$XskRestricted $ArgList"
    & $XskRestricted @ArgList

    if ($LastExitCode -ne 0) {
        Write-Error "xskrestricted failed with exit code $LastExitCode"
    }

    Write-Output "xskrestricted sample test PASSED"

} finally {
    if (!$Deprecated) {
        & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -Force -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name xskrestricted -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
