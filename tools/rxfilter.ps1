<#

.SYNOPSIS
Runs the rxfilter sample, which filters RX traffic on a virtual XDPMP adapter.

By default the eBPF-based sample (rxfilter.exe) is run. Pass -Deprecated to run
the legacy built-in rules-based sample (rxfilter-deprecated.exe) instead.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER Duration
    Duration of the test in seconds.

.PARAMETER Action
    The filter action to apply (Drop, Pass, or L2Fwd).

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
    [ValidateSet("Drop", "Pass", "L2Fwd")]
    [string]$Action = "Drop",

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
    Write-Warning "rxfilter -Deprecated uses the built-in program API (rxfilter-deprecated); this path is deprecated and planned for removal."
    $RxFilter = "$ArtifactsDir\test\rxfilter-deprecated.exe"
} else {
    #
    # Select the eBPF program that implements the requested action.
    #
    switch ($Action) {
        "Drop"  { $ProgramName = "drop" }
        "Pass"  { $ProgramName = "pass" }
        "L2Fwd" { $ProgramName = "l1fwd" }
    }
    $RxFilter = "$ArtifactsDir\test\rxfilter.exe"
    $BpfProgram = "$ArtifactsDir\test\bpf\$ProgramName.sys"
}

# Verify binaries exist.
if (!(Test-Path $RxFilter)) {
    Write-Error "$RxFilter does not exist!"
}
if (!$Deprecated -and !(Test-Path $BpfProgram)) {
    Write-Error "$BpfProgram does not exist!"
}

$RxFilterProcess = $null

try {
    & "$RootDir\tools\log.ps1" -Start -Name rxfilter -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

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
    # Start rxfilter with the requested action.
    #
    $ArgList = @("-IfIndex", $IfIndex)
    if ($Deprecated) {
        $ArgList += @("-QueueId", "*", "-MatchType", "All", "-Action", $Action, "-XdpMode", "System")
    } else {
        $ArgList += @("-BpfProgram", $BpfProgram, "-ProgramName", $ProgramName)
    }

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
        Write-Error "No packets were processed - the program may not have attached correctly"
    }

    Write-Output "rxfilter sample test PASSED"

} finally {
    if ($null -ne $RxFilterProcess -and !$RxFilterProcess.HasExited) {
        Stop-Process -Force -InputObject $RxFilterProcess -ErrorAction 'Continue'
    }
    if (!$Deprecated) {
        & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -Force -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name rxfilter -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
