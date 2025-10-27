<#

.SYNOPSIS
This script runs the XDP functional tests.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
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
    [string]$Platform = "x64",

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
    [string]$TestBinaryPath = "",

    [Parameter(Mandatory = $false)]
    [switch]$NoPrerelease = $false,

    [Parameter(Mandatory = $false)]
    [ValidateSet("MSI", "INF", "NuGet")]
    [string]$XdpInstaller = "MSI"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$LogsDir = "$RootDir\artifacts\logs"
$IterationFailureCount = 0
$IterationTimeout = 0

. $RootDir\tools\common.ps1

$VsTestPath = Get-VsTestPath
if ($VsTestPath -eq $null) {
    Write-Error "Could not find VSTest path"
}
$VsTestConsole = "vstest.console"
if ($Platform -eq "arm64") {
    $VsTestConsole = "vstest.console.arm64"
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

        & "$RootDir\tools\log.ps1" -Start -Name $LogName -Profile XdpFunctional.Verbose -Config $Config -Platform $Platform

        if (!$EbpfPreinstalled) {
            Write-Verbose "installing ebpf..."
            & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Platform $Platform
            Write-Verbose "installed ebpf."
        }

        Write-Verbose "installing xdp..."
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform -EnableEbpf -XdpInstaller $XdpInstaller
        Write-Verbose "installed xdp."

        # Debug - show registry output
        reg.exe query "HKCU\Software\ebpf" /s
        reg.exe query "HKLM\Software\ebpf" /s

        Write-Verbose "installing fnmp..."
        & "$RootDir\tools\setup.ps1" -Install fnmp -Config $Config -Platform $Platform
        Write-Verbose "installed fnmp."

        Write-Verbose "installing fnlwf..."
        & "$RootDir\tools\setup.ps1" -Install fnlwf -Config $Config -Platform $Platform
        Write-Verbose "installed fnlwf."

        Write-Verbose "installing fnsock..."
        & "$RootDir\tools\setup.ps1" -Install fnsock -Config $Config -Platform $Platform
        Write-Verbose "installed fnsock."

        $TestArgs = @()
        if (![string]::IsNullOrEmpty($TestBinaryPath)) {
            $TestArgs += $TestBinaryPath
        } else {
            $TestArgs += "$ArtifactsDir\test\xdpfunctionaltests.dll"
        }
        if (![string]::IsNullOrEmpty($TestCaseFilter)) {
            $TestArgs += "/TestCaseFilter:$TestCaseFilter"
        }
        if ($NoPrerelease) {
            $TestArgs += "/TestCaseFilter:Priority!=1"
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
                Stop-Process -Name $VsTestConsole -Force
            }
        }

        Write-Verbose "$VsTestPath\$VsTestConsole.exe $TestArgs"
        & $VsTestPath\$VsTestConsole.exe $TestArgs

        if ($LastExitCode -ne 0) {
            Write-Error "[$i/$Iterations] xdpfunctionaltests failed with $LastExitCode" -ErrorAction Continue
            $IterationFailureCount++
        }

        # Sanity test the XDP_PA installer.

        Write-Verbose "Reinstalling XDP at PA layer..."
        & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -XdpInstaller $XdpInstaller
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform -EnableEbpf -XdpInstaller $XdpInstaller -PaLayer
        Write-Verbose "Installed XDP PA layer."

        if (!(Get-NetAdapterBinding -ComponentID ms_xdp_pa)) {
            Write-Error "[$i/$Iterations] XDP_PA failed to install" -ErrorAction Continue
            $IterationFailureCount++
        }
    } finally {
        if ($Watchdog -ne $null) {
            Remove-Job -Job $Watchdog -Force
        }
        & "$RootDir\tools\setup.ps1" -Uninstall fnsock -Config $Config -Platform $Platform -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall fnlwf -Config $Config -Platform $Platform -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall fnmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -XdpInstaller $XdpInstaller -ErrorAction 'Continue'
        if (!$EbpfPreinstalled) {
            & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
        }
        & "$RootDir\tools\log.ps1" -Stop -Name $LogName -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
}

if ($IterationFailureCount -gt 0) {
    Write-Error "$IterationFailureCount of $Iterations test iterations failed"
}
