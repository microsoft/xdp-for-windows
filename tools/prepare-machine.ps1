<#

.SYNOPSIS
This prepares a machine for running XDP.

.PARAMETER Platform
    The CPU platform to use.

.PARAMETER ForBuild
    Installs all the build-time dependencies.

.PARAMETER ForEbpfBuild
    Installs all the eBPF build-time dependencies.

.PARAMETER ForTest
    Installs all the run-time dependencies.

.PARAMETER ForFunctionalTest
    Installs all the run-time dependencies and configures machine for
    functional tests.

.PARAMETER ForSpinxskTest
    Installs all the run-time dependencies and configures machine for
    spinxsk tests.

.PARAMETER ForLogging
    Installs all the logging dependencies.

.PARAMETER NoReboot
    Does not reboot the machine.

.PARAMETER RequireNoReboot
    Returns an error if a reboot is needed.

.PARAMETER Force
    Forces the installation of the latest dependencies.

#>

param (
    [ValidateSet("x64", "arm64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [switch]$ForBuild = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForEbpfBuild = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForTest = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForFunctionalTest = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForSpinxskTest = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForPerfTest = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForLogging = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoReboot = $false,

    [Parameter(Mandatory = $false)]
    [switch]$RequireNoReboot = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Force = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Cleanup = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = "$RootDir\artifacts"
$NugetDir = "$ArtifactsDir/nuget"

if (!$ForBuild -and !$ForEbpfBuild -and !$ForTest -and !$ForFunctionalTest -and !$ForSpinxskTest -and !$ForPerfTest -and !$ForLogging) {
    Write-Error 'Must one of -ForBuild, -ForTest, -ForFunctionalTest, -ForSpinxskTest, -ForPerfTest, or -ForLogging'
}

# Flag that indicates something required a reboot.
$Reboot = $false

if ($RequireNoReboot) {
    $script:NoReboot = $true
}

# Log the OS version.
Write-Verbose "Querying OS BuildLabEx"
Get-OsBuildVersionString | Write-Verbose

function Download-CoreNet-Deps {
    $CoreNetCiCommit = Get-CoreNetCiCommit

    # Download and extract https://github.com/microsoft/corenet-ci.
    if (!(Test-Path $ArtifactsDir)) { mkdir $ArtifactsDir }
    if ($Force -and (Test-Path "$ArtifactsDir/corenet-ci-$CoreNetCiCommit")) {
        Remove-Item -Recurse -Force "$ArtifactsDir/corenet-ci-$CoreNetCiCommit"
    }
    if (!(Test-Path "$ArtifactsDir/corenet-ci-$CoreNetCiCommit")) {
        Remove-Item -Recurse -Force "$ArtifactsDir/corenet-ci-*"
        Invoke-WebRequest-WithRetry -Uri "https://github.com/microsoft/corenet-ci/archive/$CoreNetCiCommit.zip" -OutFile "$ArtifactsDir\corenet-ci.zip"
        Expand-Archive -Path "$ArtifactsDir\corenet-ci.zip" -DestinationPath $ArtifactsDir -Force
        Remove-Item -Path "$ArtifactsDir\corenet-ci.zip"
    }
}

function Download-eBpf-Nuget {
    param (
        [Parameter()]
        [string]$Platform
    )
    # Download private eBPF Nuget package.
    $EbpfVersion = Get-EbpfVersion
    $EbpfNugetVersion = "eBPF-for-Windows.$Platform.$EbpfVersion"
    $EbpfNuget = "$EbpfNugetVersion.nupkg"
    $EbpfNugetUrl = "https://github.com/microsoft/ebpf-for-windows/releases/download/Release-v$EbpfVersion/$EbpfNuget"
    $EbpfNugetRestoreDir = "$RootDir/packages/$EbpfNugetVersion"

    if ($Force -and (Test-Path $NugetDir)) {
        Remove-Item -Recurse -Force $NugetDir
    }
    if (!(Test-Path $NugetDir)) {
        mkdir $NugetDir | Write-Verbose
    }

    if (!(Test-Path $NugetDir/$EbpfNuget)) {
        # Remove any old builds of the package.
        if (Test-Path $EbpfNugetRestoreDir) {
            Remove-Item -Recurse -Force $EbpfNugetRestoreDir
        }
        Remove-Item -Force $NugetDir/$EbpfNugetVersion*

        Invoke-WebRequest-WithRetry -Uri $EbpfNugetUrl -OutFile $NugetDir/$EbpfNuget
    }
}

function Download-Ebpf-Msi {
    # Download and extract private eBPF installer MSI package.
    $EbpfMsiFullPath = Get-EbpfMsiFullPath -Platform $Platform

    if (!(Test-Path $EbpfMsiFullPath)) {
        $EbpfMsiDir = Split-Path $EbpfMsiFullPath
        $EbpfMsiUrl = Get-EbpfMsiUrl -Platform $Platform

        if (!(Test-Path $EbpfMsiDir)) {
            mkdir $EbpfMsiDir | Write-Verbose
        }

        Invoke-WebRequest-WithRetry -Uri $EbpfMsiUrl -OutFile $EbpfMsiFullPath
    }
}

function Download-Fn-Runtime {
    $FnRuntimeUrl = Get-FnRuntimeUrl -Platform $Platform
    $FnRuntimeDir = Get-FnRuntimeDir
    $FnRuntimeZip = "$FnRuntimeDir/runtime.zip"

    if ($Force -and (Test-Path $FnRuntimeDir)) {
        Remove-Item -Recurse -Force $FnRuntimeDir
    }
    if (!(Test-Path $FnRuntimeDir)) {
        mkdir $FnRuntimeDir | Write-Verbose

        Write-Verbose "Downloading Fn runtime"
        Invoke-WebRequest-WithRetry -Uri $FnRuntimeUrl -OutFile $FnRuntimeZip
        Expand-Archive -Path $FnRuntimeZip -DestinationPath $FnRuntimeDir -Force
        Remove-Item -Path $FnRuntimeZip
    }
}

function Setup-TestSigning {
    # Check to see if test signing is enabled.
    $HasTestSigning = $false
    try { $HasTestSigning = ("$(bcdedit)" | Select-String -Pattern "testsigning\s+Yes").Matches.Success } catch { }

    # Enable test signing as necessary.
    if (!$HasTestSigning) {
        # Enable test signing.
        Write-Host "Enabling Test Signing. Reboot required!"
        bcdedit /set testsigning on | Write-Verbose
        if ($NoReboot) {
            Write-Warning "Enabling Test Signing requires reboot, but -NoReboot option specified."
        } else {
            $Script:Reboot = $true
        }
    }
}

function Setup-VcRuntime {
    $Installed = $false
    try { $Installed = Get-ChildItem -Path Registry::HKEY_CLASSES_ROOT\Installer\Dependencies | Where-Object { $_.Name -like "*VC,redist*" } } catch {}

    if (!$Installed -or $Force) {
        Write-Host "Installing VC++ runtime"

        if (!(Test-Path $ArtifactsDir)) { mkdir artifacts }
        Remove-Item -Force "$ArtifactsDir\vc_redist.$Platform.exe" -ErrorAction Ignore

        # Download and install.
        Invoke-WebRequest-WithRetry -Uri "https://aka.ms/vs/17/release/vc_redist.$Platform.exe" -OutFile "$ArtifactsDir\vc_redist.$Platform.exe"
        & $ArtifactsDir\vc_redist.$Platform.exe /install /passive | Write-Verbose
    }
}

function Setup-VsTest {
    if (!(Get-VsTestPath) -or $Force) {
        Write-Host "Installing VsTest"

        if (!(Test-Path $ArtifactsDir)) { mkdir $ArtifactsDir }
        Remove-Item -Recurse -Force "$ArtifactsDir\Microsoft.TestPlatform" -ErrorAction Ignore

        # Download and extract.
        Invoke-WebRequest-WithRetry -Uri "https://www.nuget.org/api/v2/package/Microsoft.TestPlatform/17.11.1" -OutFile "$ArtifactsDir\Microsoft.TestPlatform.zip"
        Expand-Archive -Path "$ArtifactsDir\Microsoft.TestPlatform.zip" -DestinationPath "$ArtifactsDir\Microsoft.TestPlatform" -Force
        Remove-Item -Path "$ArtifactsDir\Microsoft.TestPlatform.zip"

        # Add to PATH.
        $RootDir = Split-Path $PSScriptRoot -Parent
        $Path = [Environment]::GetEnvironmentVariable("Path", "Machine")
        $Path += ";$(Get-VsTestPath)"
        [Environment]::SetEnvironmentVariable("Path", $Path, "Machine")
        $Env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
    }
}

function Enable-CrashDumps {
    $CrashControl = Get-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl

    if ($CrashControl.CrashDumpEnabled -ne 1) {
        # Enable complete (kernel + user) system crash dumps
        Write-Verbose "reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f"
        reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f
        $script:Reboot = $true
    }

    if (!($CrashControl.PSobject.Properties.name -match "AlwaysKeepMemoryDump") -or $CrashControl.AlwaysKeepMemoryDump -ne 1) {
        # Always retain crash dumps
        Write-Verbose "reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v AlwaysKeepMemoryDump /d 1 /t REG_DWORD /f"
        reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v AlwaysKeepMemoryDump /d 1 /t REG_DWORD /f
        $script:Reboot = $true
    }
}

if ($Cleanup) {
    if ($ForTest) {
        # Tests do not fully clean up.
    }
} else {
    if ($ForBuild) {
        if (!(Test-Path $NugetDir)) {
            mkdir $NugetDir | Write-Verbose
        }

        if ($Platform -eq "arm64") {
            # Download prerelease versions for arm64, and a matching x64 for
            # local cross-compile (bpf2c) binaries.
            Download-eBpf-Nuget -Platform arm64
            Download-eBpf-Nuget -Platform x64
        }
    }

    if ($ForEbpfBuild) {
        if (!(Get-Command clang.exe)) {
            Write-Error "clang.exe is not detected"
        }
    }

    if ($ForFunctionalTest) {
        $ForTest = $true
        # Verifier configuration: standard flags on all XDP components, and NDIS.
        # The NDIS verifier is required, otherwise allocations NDIS makes on
        # behalf of XDP components (e.g. NBLs) will not be verified.
        Write-Verbose "verifier.exe /standard /driver xdp.sys fnmp.sys fnlwf.sys ndis.sys ebpfcore.sys"
        verifier.exe /standard /driver xdp.sys fnmp.sys fnlwf.sys ndis.sys ebpfcore.sys | Write-Verbose
        if (!$?) {
            $Reboot = $true
        }

        Enable-CrashDumps
        Download-Fn-Runtime
        Write-Verbose "$(Get-FnRuntimeDir)/tools/prepare-machine.ps1 -ForTest -NoReboot"
        $FnResult = & "$(Get-FnRuntimeDir)/tools/prepare-machine.ps1" -ForTest -NoReboot
        if ($null -ne $FnResult -and $FnResult -contains "RebootRequired" -and $FnResult["RebootRequired"]) {
            $Reboot = $true
        }
    }

    if ($ForSpinxskTest) {
        $ForTest = $true
        # Verifier configuration: standard flags with low resources simulation.
        # 599 - Failure probability (599/10000 = 5.99%)
        #       N.B. If left to the default value, roughly every 5 minutes verifier
        #       will fail all allocations within a 10 second interval. This behavior
        #       complicates the spinxsk socket setup statistics. Setting it to a
        #       non-default value disables this behavior.
        # ""  - Pool tag filter
        # ""  - Application filter
        # 1   - Delay (in minutes) after boot until simulation engages
        #       This is the lowest value configurable via verifier.exe.
        # WARNING: xdp.sys itself may fail to load due to low resources simulation.
        Write-Verbose "verifier.exe /standard /faults 599 `"`" `"`" 1  /driver xdp.sys ebpfcore.sys"
        verifier.exe /standard /faults 599 `"`" `"`" 1  /driver xdp.sys ebpfcore.sys | Write-Verbose
        if (!$?) {
            $Reboot = $true
        }

        Enable-CrashDumps
        Download-Fn-Runtime
        Write-Verbose "$(Get-FnRuntimeDir)/tools/prepare-machine.ps1 -ForTest -NoReboot"
        $FnResult = & "$(Get-FnRuntimeDir)/tools/prepare-machine.ps1" -ForTest -NoReboot
        if ($null -ne $FnResult -and $FnResult -contains "RebootRequired" -and $FnResult["RebootRequired"]) {
            $Reboot = $true
        }
    }

    if ($ForPerfTest) {
        $ForTest = $true

        Write-Verbose "verifier.exe /reset"
        verifier.exe /reset | Write-Verbose
        if (!$?) {
            $Reboot = $true
        }
    }

    if ($ForTest) {
        Setup-VcRuntime
        Setup-VsTest
        Download-CoreNet-Deps
        Download-Ebpf-Msi
        Setup-TestSigning
    }

    if ($ForLogging) {
        Download-CoreNet-Deps
    }
}

if ($Reboot) {
    if ($RequireNoReboot) {
        Write-Error "Reboot required but disallowed"
        Write-Info "If running in CI, it likely means the OS image used has a non-matching configuration."
    } elseif ($NoReboot) {
        Write-Verbose "Reboot required"
        return @{"RebootRequired" = $true}
    } else {
        Write-Host "Rebooting..."
        shutdown.exe /f /r /t 0
    }
} else {
    return $null
}
