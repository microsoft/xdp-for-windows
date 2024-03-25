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
    [int]$Iterations = 1,

    [Parameter(Mandatory = $false)]
    [switch]$EbpfPreinstalled = $false,

    [Parameter(Mandatory = $false)]
    [int]$Timeout = 0,

    [Parameter(Mandatory = $false)]
    [string]$TestBinaryPath = ""
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$LogsDir = "$RootDir\artifacts\logs"
$IterationFailureCount = 0
$IterationTimeout = 0

. $RootDir\tools\common.ps1

$VsTestPath = Get-VsTestPath
if ($VsTestPath -eq $null) {
    Write-Error "Could not find VSTest path"
}

if ($Timeout -gt 0) {
    $WatchdogReservedMinutes = 2
    $IterationTimeout = $Timeout / $Iterations - $WatchdogReservedMinutes

    if ($IterationTimeout -le 0) {
        Write-Error "Timeout must allow at least $WatchdogReservedMinutes minutes per iteration"
    }
}

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

for ($i = 1; $i -le $Iterations; $i++) {
    try {
        $Watchdog = $null
        $LogName = "xdpfunc"
        if ($Iterations -gt 1) {
            $LogName += "-$i"
        }

        & "$RootDir\tools\log.ps1" -Start -Name $LogName -Profile XdpFunctional.Verbose -Config $Config -Arch $Arch

        Write-Verbose "installing xdp..."
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Arch $Arch -EnableEbpf
        Write-Verbose "installed xdp."

        Write-Verbose "installing fnmp..."
        & "$RootDir\tools\setup.ps1" -Install fnmp -Config $Config -Arch $Arch
        Write-Verbose "installed fnmp."

        Write-Verbose "installing fnlwf..."
        & "$RootDir\tools\setup.ps1" -Install fnlwf -Config $Config -Arch $Arch
        Write-Verbose "installed fnlwf."

        if (!$EbpfPreinstalled) {
            Write-Verbose "installing ebpf..."
            & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Arch $Arch
            Write-Verbose "installed ebpf."
        }

        $TestArgs = @()
        if (![string]::IsNullOrEmpty($TestBinaryPath)) {
            $TestArgs += $TestBinaryPath
        } else {
            $TestArgs += "$ArtifactsDir\xdpfunctionaltests.dll"
        }
        if (![string]::IsNullOrEmpty($TestCaseFilter)) {
            $TestArgs += "/TestCaseFilter:$TestCaseFilter"
        }
        if ($ListTestCases) {
            $TestArgs += "/lt"
        }
        $TestArgs += "/logger:trx"
        $TestArgs += "/ResultsDirectory:$LogsDir"

        if ($IterationTimeout -gt 0) {
            $Watchdog = Start-Job -ScriptBlock {
                Start-Sleep -Seconds (60 * $Using:IterationTimeout)

                . $Using:RootDir\tools\common.ps1
                Collect-LiveKD -OutFile "$Using:LogsDir\$Using:LogName-livekd.dmp"
                Collect-ProcessDump -ProcessName "testhost.exe" -OutFile "$Using:LogsDir\$Using:LogName-testhost.dmp"
                Stop-Process -Name "vstest.console" -Force
            }
        }

        Write-Verbose "$VsTestPath\vstest.console.exe $TestArgs"
        & $VsTestPath\vstest.console.exe $TestArgs

        if ($LastExitCode -ne 0) {
            Write-Error "[$i/$Iterations] xdpfunctionaltests failed with $LastExitCode" -ErrorAction Continue
            $IterationFailureCount++
        }
    } finally {
        if ($Watchdog -ne $null) {
            Remove-Job -Job $Watchdog -Force
        }
        if (!$EbpfPreinstalled) {
            & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        }
        & "$RootDir\tools\setup.ps1" -Uninstall fnlwf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall fnmp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\log.ps1" -Stop -Name $LogName -Config $Config -Arch $Arch -ErrorAction 'Continue'
    }
}

if ($IterationFailureCount -gt 0) {
    Write-Error "$IterationFailureCount of $Iterations test iterations failed"
}
