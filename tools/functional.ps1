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

.PARAMETER UserMode
    Run user mode tests.

.PARAMETER KernelMode
    Run kernel mode tests.

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
    [string]$TestBinaryPath = "",

    [Parameter(Mandatory = $false)]
    [switch]$UserMode = $false,

    [Parameter(Mandatory = $false)]
    [switch]$KernelMode = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoPrerelease = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

function CleanupKernelMode {
    sc.exe stop xdpfunctionaltestdrv | Write-Verbose
    sc.exe delete xdpfunctionaltestdrv | Write-Verbose
    Remove-Item -Path "$SystemDriversPath\xdpfunctionaltestdrv.sys" -ErrorAction SilentlyContinue
    [Environment]::SetEnvironmentVariable("xdpfunctionaltests::KernelModeEnabled", $null)
    [Environment]::SetEnvironmentVariable("xdpfunctionaltests::KernelModeDriverPath", $null)
}

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Arch $Arch
$LogsDir = "$RootDir\artifacts\logs"
$IterationFailureCount = 0
$IterationTimeout = 0
$SystemDriversPath = Join-Path $([Environment]::GetEnvironmentVariable("SystemRoot")) "System32\drivers"

. $RootDir\tools\common.ps1

$VsTestPath = Get-VsTestPath
if ($VsTestPath -eq $null) {
    Write-Error "Could not find VSTest path"
}

if ($UserMode -and $KernelMode) {
    Write-Error "Only one of -UserMode and -KernelMode is supported"
}

if ($Timeout -gt 0) {
    $WatchdogReservedMinutes = 2
    $IterationTimeout = $Timeout / $Iterations - $WatchdogReservedMinutes

    if ($IterationTimeout -le 0) {
        Write-Error "Timeout must allow at least $WatchdogReservedMinutes minutes per iteration"
    }
}

if ($KernelMode) {
    # Ensure clean slate.
    CleanupKernelMode
    [System.Environment]::SetEnvironmentVariable('xdpfunctionaltests::KernelModeEnabled', '1')
    [System.Environment]::SetEnvironmentVariable('xdpfunctionaltests::KernelModeDriverPath', "$SystemDriversPath\")
    Copy-Item -Path "$ArtifactsDir\test\xdpfunctionaltestdrv\xdpfunctionaltestdrv.sys" $SystemDriversPath
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

        if (!$EbpfPreinstalled) {
            Write-Verbose "installing ebpf..."
            & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Arch $Arch # -UseJitEbpf:$UseJitEbpf
            Write-Verbose "installed ebpf."
        }

        Write-Verbose "installing xdp..."
        & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Arch $Arch -EnableEbpf -EnableKmXdpApi:$KernelMode
        Write-Verbose "installed xdp."

        Write-Verbose "installing fnmp..."
        & "$RootDir\tools\setup.ps1" -Install fnmp -Config $Config -Arch $Arch
        Write-Verbose "installed fnmp."

        Write-Verbose "installing fnlwf..."
        & "$RootDir\tools\setup.ps1" -Install fnlwf -Config $Config -Arch $Arch
        Write-Verbose "installed fnlwf."

        Write-Verbose "installing fnsock..."
        & "$RootDir\tools\setup.ps1" -Install fnsock -Config $Config -Arch $Arch
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
        & "$RootDir\tools\setup.ps1" -Uninstall fnsock -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall fnlwf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall fnmp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        if ($KernelMode) {
            CleanupKernelMode
        }
        if (!$EbpfPreinstalled) {
            & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Arch $Arch -ErrorAction 'Continue'
        }
        & "$RootDir\tools\log.ps1" -Stop -Name $LogName -Config $Config -Arch $Arch -ErrorAction 'Continue'
    }
}

if ($IterationFailureCount -gt 0) {
    Write-Error "$IterationFailureCount of $Iterations test iterations failed"
}
