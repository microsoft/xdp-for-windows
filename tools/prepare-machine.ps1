<#

.SYNOPSIS
This prepares a machine for running XDP.

.PARAMETER ForBuild
    Installs all the build-time dependencies.

.PARAMETER ForTest
    Installs all the run-time dependencies.

.PARAMETER NoReboot
    Does not reboot the machine.

.PARAMETER Force
    Forces the installation of the latest dependencies.

#>

param (
    [Parameter(Mandatory = $false)]
    [switch]$ForBuild = $false,

    [Parameter(Mandatory = $false)]
    [switch]$ForTest = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoReboot = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Force = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Cleanup = $false
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

if (!$ForBuild -and !$ForTest) {
    Write-Error 'Must specify either -ForBuild or -ForTest'
}

# Flag that indicates something required a reboot.
$Reboot = $false

function Download-CoreNet-Deps {
    # Download and extract https://github.com/microsoft/corenet-ci.
    if (!(Test-Path "artifacts")) { mkdir artifacts }
    if ($Force -and (Test-Path "artifacts/corenet-ci")) {
        Remove-Item -Recurse -Force "artifacts/corenet-ci-main"
    }
    if (!(Test-Path "artifacts/corenet-ci-main")) {
        Invoke-WebRequest -Uri "https://github.com/microsoft/corenet-ci/archive/refs/heads/main.zip" -OutFile "artifacts\corenet-ci.zip"
        Expand-Archive -Path "artifacts\corenet-ci.zip" -DestinationPath "artifacts" -Force
        Remove-Item -Path "artifacts\corenet-ci.zip"
    }
}

function Setup-TestSigning {
    # Check to see if test signing is enabled.
    $HasTestSigning = $false
    try { $HasTestSigning = ("$(bcdedit)" | Select-String -Pattern "testsigning\s+Yes").Matches.Success } catch { }

    # Enable test signing as necessary.
    if (!$HasTestSigning) {
        if ($NoReboot) {
            Write-Error "Test Signing Not Enabled!"
        } else {
            # Enable test signing.
            Write-Host "Enabling Test Signing. Reboot required!"
            bcdedit /set testsigning on
            $Script:Reboot = $true
        }
    }
}

# Installs the XDP certificates.
function Install-Certs {
    $CodeSignCertPath = "artifacts\CoreNetSignRoot.cer"
    if (!(Test-Path $CodeSignCertPath)) {
        Write-Error "$CodeSignCertPath does not exist!"
    }
    CertUtil.exe -f -addstore Root $CodeSignCertPath
    CertUtil.exe -f -addstore trustedpublisher $CodeSignCertPath
}

# Uninstalls the XDP certificates.
function Uninstall-Certs {
    try { CertUtil.exe -delstore Root "CoreNetTestSigning" } catch { }
    try { CertUtil.exe -delstore trustedpublisher "CoreNetTestSigning" } catch { }
}

if ($Cleanup) {
    if ($ForTest) {
        Uninstall-Certs
    }
} else {
    if ($ForBuild) {
        Download-CoreNet-Deps
        Copy-Item artifacts\corenet-ci-main\vm-setup\CoreNetSignRoot.cer artifacts\CoreNetSignRoot.cer
        Copy-Item artifacts\corenet-ci-main\vm-setup\CoreNetSign.pfx artifacts\CoreNetSign.pfx
    }

    if ($ForTest) {
        Setup-TestSigning
        Download-CoreNet-Deps
        Copy-Item artifacts\corenet-ci-main\vm-setup\CoreNetSignRoot.cer artifacts\CoreNetSignRoot.cer
        Copy-Item artifacts\corenet-ci-main\vm-setup\CoreNetSign.pfx artifacts\CoreNetSign.pfx
        Copy-Item artifacts\corenet-ci-main\vm-setup\dswdevice.exe C:\dswdevice.exe
        Copy-Item artifacts\corenet-ci-main\vm-setup\kd.exe C:\kd.exe
        Copy-Item artifacts\corenet-ci-main\vm-setup\livekd64.exe C:\livekd64.exe
        Copy-Item artifacts\corenet-ci-main\vm-setup\notmyfault64.exe C:\notmyfault64.exe
        Install-Certs
    }
}

if ($Reboot -and !$NoReboot) {
    # Reboot the machine.
    Write-Host "Rebooting..."
    shutdown.exe /f /r /t 0
}
