<#

.SYNOPSIS
This prepares a machine for running XDP.

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

.PARAMETER Force
    Forces the installation of the latest dependencies.

#>

param (
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
    [switch]$Force = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Cleanup = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = "$RootDir\artifacts"

if (!$ForBuild -and !$ForEbpfBuild -and !$ForTest -and !$ForFunctionalTest -and !$ForSpinxskTest -and !$ForPerfTest -and !$ForLogging) {
    Write-Error 'Must one of -ForBuild, -ForTest, -ForFunctionalTest, -ForSpinxskTest, -ForPerfTest, or -ForLogging'
}

$EbpfNugetVersion = "eBPF-for-Windows.0.9.0"
$EbpfNugetBuild = "+5122375852"
$EbpfNuget = "$EbpfNugetVersion$EbpfNugetBuild.nupkg"
$EbpfNugetUrl = "https://github.com/microsoft/xdp-for-windows/releases/download/main-prerelease/$EbpfNugetVersion$EbpfNugetBuild.nupkg"
$EbpfNugetRestoreDir = "$RootDir/packages/$EbpfNugetVersion"

# Flag that indicates something required a reboot.
$Reboot = $false

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
    # Download and extract private eBPF Nuget package.
    $NugetDir = "$ArtifactsDir/nuget"
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
    $EbpfMsiUrl = Get-EbpfMsiUrl
    $EbpfMsiFullPath = Get-EbpfMsiFullPath

    if (!(Test-Path $EbpfMsiFullPath)) {
        $EbpfMsiDir = Split-Path $EbpfMsiFullPath
        if (!(Test-Path $EbpfMsiDir)) {
            mkdir $EbpfMsiDir | Write-Verbose
        }

        Invoke-WebRequest-WithRetry -Uri $EbpfMsiUrl -OutFile $EbpfMsiFullPath
    }
}

function Download-Fn-Runtime {
    $FnRuntimeUrl = Get-FnRuntimeUrl
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

# Installs the XDP certificates.
function Install-Certs {
    $CodeSignCertPath = Get-CoreNetCiArtifactPath -Name "CoreNetSignRoot.cer"
    if (!(Test-Path $CodeSignCertPath)) {
        Write-Error "$CodeSignCertPath does not exist!"
    }
    CertUtil.exe -f -addstore Root $CodeSignCertPath | Write-Verbose
    CertUtil.exe -f -addstore trustedpublisher $CodeSignCertPath | Write-Verbose
}

# Uninstalls the XDP certificates.
function Uninstall-Certs {
    try { CertUtil.exe -delstore Root "CoreNetTestSigning" } catch { }
    try { CertUtil.exe -delstore trustedpublisher "CoreNetTestSigning" } catch { }
}

function Setup-VcRuntime {
    $Installed = $false
    try { $Installed = Get-ChildItem -Path Registry::HKEY_CLASSES_ROOT\Installer\Dependencies | Where-Object { $_.Name -like "*VC,redist*" } } catch {}

    if (!$Installed -or $Force) {
        Write-Host "Installing VC++ runtime"

        if (!(Test-Path $ArtifactsDir)) { mkdir artifacts }
        Remove-Item -Force "$ArtifactsDir\vc_redist.x64.exe" -ErrorAction Ignore

        # Download and install.
        Invoke-WebRequest-WithRetry -Uri "https://aka.ms/vs/16/release/vc_redist.x64.exe" -OutFile "$ArtifactsDir\vc_redist.x64.exe"
        & $ArtifactsDir\vc_redist.x64.exe /install /passive | Write-Verbose
    }
}

function Setup-VsTest {
    if (!(Get-VsTestPath) -or $Force) {
        Write-Host "Installing VsTest"

        if (!(Test-Path $ArtifactsDir)) { mkdir $ArtifactsDir }
        Remove-Item -Recurse -Force "$ArtifactsDir\Microsoft.TestPlatform" -ErrorAction Ignore

        # Download and extract.
        Invoke-WebRequest-WithRetry -Uri "https://www.nuget.org/api/v2/package/Microsoft.TestPlatform/16.11.0" -OutFile "$ArtifactsDir\Microsoft.TestPlatform.zip"
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

function Install-AzStorageModule {
    if (!(Get-PackageProvider -ListAvailable -Name NuGet -ErrorAction Ignore)) {
        Write-Host "Installing NuGet package provider"
        Install-PackageProvider -Name NuGet -Force | Write-Verbose
    }
    if (!(Get-Module -ListAvailable -Name Az.Storage)) {
        Write-Host "Installing Az.Storage module"
        Install-Module Az.Storage -Repository PSGallery -Scope CurrentUser -AllowClobber -Force | Write-Verbose
    }
    # AzureRM is installed by default on some CI images and is incompatible with
    # Az. Uninstall.
    Uninstall-AzureRm
}

if ($Cleanup) {
    if ($ForTest) {
        Uninstall-Certs
    }
} else {
    if ($ForBuild) {
        Download-CoreNet-Deps
        Download-eBpf-Nuget
    }

    if ($ForEbpfBuild) {
        if (!(Get-Command clang.exe)) {
            Write-Error "clang.exe is not detected"
        }

        if (!(cmd /c "clang --version 2>&1" | Select-String "clang version 11.")) {
            Write-Error "Compiling eBPF programs on Windows requires clang version 11"
        }

        $EbpfExportProgram = "$EbpfNugetRestoreDir/build/native/bin/export_program_info.exe"

        if (!(Test-Path $EbpfExportProgram)) {
            Write-Error "Missing eBPF helper export_program_info.exe. Is the NuGet package installed?"
        }

        Write-Verbose $EbpfExportProgram
        & $EbpfExportProgram | Write-Verbose
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

        if ((Get-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl).CrashDumpEnabled -ne 1) {
            # Enable complete (kernel + user) system crash dumps
            Write-Verbose "reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f"
            reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f
            $Reboot = $true
        }

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

        if ((Get-ItemProperty -Path HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl).CrashDumpEnabled -ne 1) {
            # Enable complete (kernel + user) system crash dumps
            Write-Verbose "reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f"
            reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f
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

        Install-AzStorageModule
    }

    if ($ForTest) {
        Setup-VcRuntime
        Setup-VsTest
        Download-CoreNet-Deps
        Download-Ebpf-Msi
        Setup-TestSigning
        Install-Certs
    }

    if ($ForLogging) {
        Download-CoreNet-Deps
    }
}

if ($Reboot -and !$NoReboot) {
    # Reboot the machine.
    Write-Host "Rebooting..."
    shutdown.exe /f /r /t 0
} elseif ($Reboot) {
    Write-Host "Reboot required."
}
