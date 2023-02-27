<#

.SYNOPSIS
This script runs the XDP functional tests.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER TestCaseFilter
    The test case filter passed to VSTest.

.PARAMETER Iterations
    The number of times to run the test suite.

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
    [switch]$ListTestCases = $false,

    [Parameter(Mandatory = $false)]
    [int]$Iterations = 1
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$LogsDir = "$RootDir\artifacts\logs"
$IterationFailureCount = 0

. $RootDir\tools\common.ps1

$VsTestPath = Get-VsTestPath
if ($VsTestPath -eq $null) {
    Write-Error "Could not find VSTest path"
}

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

for ($i = 1; $i -le $Iterations; $i++) {
    try {
        $LogName = "xdpfunc"
        if ($Iterations -gt 1) {
            $LogName += "-$i"
        }

        & "$RootDir\tools\log.ps1" -Start -Name $LogName -Profile XdpFunctional.Verbose -Config $Config -Arch $Arch

        Write-Verbose "installing xdp..."
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Arch $Arch -EnableEbpf
        Write-Verbose "installed xdp."

        Write-Verbose "installing xdpfnmp..."
        & "$RootDir\tools\setup.ps1" -Install xdpfnmp -Config $Config -Arch $Arch
        Write-Verbose "installed xdpfnmp."

        Write-Verbose "installing xdpfnlwf..."
        & "$RootDir\tools\setup.ps1" -Install xdpfnlwf -Config $Config -Arch $Arch
        Write-Verbose "installed xdpfnlwf."

        Write-Verbose "installing ebpf..."
        & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Arch $Arch
        Write-Verbose "installed ebpf."

        $Args = @("$ArtifactsDir\xdpfunctionaltests.dll")
        if (![string]::IsNullOrEmpty($TestCaseFilter)) {
            $Args += "/TestCaseFilter:$TestCaseFilter"
        }
        if ($ListTestCases) {
            $Args += "/lt"
        }
        $Args += "/logger:trx"
        $Args += "/ResultsDirectory:$LogsDir"

        Write-Verbose "$VsTestPath\vstest.console.exe $Args"
        & $VsTestPath\vstest.console.exe $Args
        if ($LastExitCode -ne 0) {
            Write-Error "[$i/$Iterations] xdpfunctionaltests failed with $LastExitCode" -ErrorAction 'Continue'
            $IterationFailureCount++
        }
    } finally {
        & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall xdpfnlwf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall xdpfnmp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\log.ps1" -Stop -Name $LogName -Config $Config -Arch $Arch -ErrorAction 'Continue'
    }
}

if ($IterationFailureCount -gt 0) {
    Write-Error "$IterationFailureCount of $Iterations test iterations failed"
}
