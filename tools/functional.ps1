<#

.SYNOPSIS
This script runs the XDP functional tests.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER TestCaseFilter
    The test case filter passed to VSTest.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [string]$TestCaseFilter = "",

    [Parameter(Mandatory = $false)]
    [switch]$ListTestCases = $false
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$LogsDir = "$RootDir\artifacts\logs"

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

try {
    & "$RootDir\tools\log.ps1" -Start -Name xdpfunc -Profile XdpFunctional.Verbose -Config $Config -Arch $Arch
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Arch $Arch

    Write-Verbose "installing xdpfnmp..."
    & "$RootDir\tools\setup.ps1" -Install xdpfnmp -Config $Config -Arch $Arch
    Write-Verbose "installed xdpfnmp."

    Write-Verbose "installing xdpfnlwf..."
    & "$RootDir\tools\setup.ps1" -Install xdpfnlwf -Config $Config -Arch $Arch
    Write-Verbose "installed xdpfnlwf."

    $Args = @("$ArtifactsDir\xdpfunctionaltests.dll")
    if (![string]::IsNullOrEmpty($TestCaseFilter)) {
        $Args += "/TestCaseFilter:$TestCaseFilter"
    }
    if ($ListTestCases) {
        $Args += "/lt"
    }

    Write-Verbose "vstest.console.exe $Args"
    vstest.console.exe $Args
    if ($LastExitCode -ne 0) {
        throw "xdpfunctionaltests failed with $LastExitCode"
    }
} finally {
    & "$RootDir\tools\setup.ps1" -Uninstall xdpfnlwf -Config $Config -Arch $Arch -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdpfnmp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    & "$RootDir\tools\log.ps1" -Stop -Name xdpfunc -Config $Config -Arch $Arch -ErrorAction 'Continue'
}
