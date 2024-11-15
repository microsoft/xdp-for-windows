<#

.SYNOPSIS
This script runs the XskFwdKm sample driver.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

try {
    & "$RootDir\tools\log.ps1" -Start -Name xskfwdkm -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform
    Write-Verbose "installed xdp."

    Write-Verbose "installing fndis..."
    & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Platform $Platform
    Write-Verbose "installed fndis."

    Write-Verbose "installing xdpmp..."
    & "$RootDir\tools\setup.ps1" -Install xdpmp -Config $Config -Platform $Platform -XdpmpPollProvider FNDIS
    Write-Verbose "installed xdpmp."

    Write-Verbose "installing xskfwdkm..."
    & "$RootDir\tools\setup.ps1" -Install xskfwdkm -Config $Config -Platform $Platform
    Write-Verbose "installed xskfwdkm."

    Stop-Service xskfwdkm

    $IfIndex = (Get-NetAdapter XDPMP).ifIndex
    Write-Verbose "reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xskfwdkm /v IfIndex /d $IfIndex /t REG_DWORD /f"
    reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xskfwdkm /v IfIndex /d $IfIndex /t REG_DWORD /f | Write-Verbose

    Write-Verbose "reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xskfwdkm /v RunInline /d 1 /t REG_DWORD /f"
    reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xskfwdkm /v RunInline /d 1 /t REG_DWORD /f | Write-Verbose

    # If XskFwdKm fails to run an iteration inline, it will fail to start.
    Start-Service xskfwdkm
} finally {
    & "$RootDir\tools\setup.ps1" -Uninstall xskfwdkm -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name xskfwdkm -Config $Config -Platform $Platform -ErrorAction 'Continue'
}
