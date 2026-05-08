<#

.SYNOPSIS
This script runs the xskmaprx sample as a smoke test: launches the sample,
lets it create an XSKMAP and attach an XDP program for a few seconds, then
terminates it.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER TimeoutSeconds
    Number of seconds to let the sample run before exiting.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [Int32]$TimeoutSeconds = 5,

    [Parameter(Mandatory = $false)]
    [string]$ComputerName = "",

    [Parameter(Mandatory = $false)]
    [System.Management.Automation.PSCredential]$Credential,

    [Parameter(Mandatory = $false)]
    [string]$RemoteRoot = "",

    [Parameter(Mandatory = $false)]
    [switch]$SkipDeploy
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Forwarded = Invoke-XdpRemoteIfRequested -InvocationCommand $MyInvocation.MyCommand `
    -BoundParameters $PSBoundParameters -Config $Config -Platform $Platform
if ($Forwarded -is [array]) { $Forwarded = $Forwarded[-1] }
if ($Forwarded) { return }
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$XskMapRx = "$ArtifactsDir\test\xskmaprx.exe"

try {
    & "$RootDir\tools\log.ps1" -Start -Name xskmaprx -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform
    Write-Verbose "installed xdp."

    Write-Verbose "installing xdpmp..."
    & "$RootDir\tools\setup.ps1" -Install xdpmp -Config $Config -Platform $Platform
    Write-Verbose "installed xdpmp."

    $IfIndex = (Get-NetAdapter XDPMP).ifIndex
    $QueueCount = 2

    Write-Verbose "Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount"
    Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount

    $ArgList =
        "-IfIndex", $IfIndex,
        "-QueueCount", $QueueCount,
        "-XdpMode", "Generic",
        "-TimeoutSeconds", $TimeoutSeconds
    Write-Verbose "$XskMapRx $ArgList"

    # The sample exits cleanly on its own after -TimeoutSeconds. Wait for it
    # and propagate its exit code.
    $Process = Start-Process $XskMapRx -PassThru -ArgumentList $ArgList -NoNewWindow -Wait
    if ($Process.ExitCode -ne 0) {
        throw "xskmaprx exited with code $($Process.ExitCode)"
    }
} finally {
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name xskmaprx -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
