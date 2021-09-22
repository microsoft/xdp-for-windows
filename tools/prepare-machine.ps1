<#

.SYNOPSIS
This prepares a machine for running XDP.

#>

param (
    [Parameter(Mandatory = $false)]
    [switch]$NoReboot = $false
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

$Reboot = $false

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
    }
} else {
    Write-Host "Test Signing Already Enabled!"
}

# Download and extract https://github.com/microsoft/corenet-ci.
Invoke-WebRequest -Uri "https://github.com/microsoft/corenet-ci/archive/refs/heads/main.zip" -OutFile "artifacts\corenet-ci.zip"
Expand-Archive -Path "artifacts\corenet-ci.zip" -DestinationPath "artifacts" -Force

# Copy the necessary files.
Copy-Item artifacts\corenet-ci-main\vm-setup\dswdevice.exe C:\dswdevice.exe
Copy-Item artifacts\corenet-ci-main\vm-setup\notmyfault64.exe C:\notmyfault64.exe
Copy-Item artifacts\corenet-ci-main\vm-setup\sdk\* "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x86\" -Force

if ($Reboot) {
    # Reboot the machine.
    Write-Host "Rebooting..."
    shutdown.exe /f /r /t 0
}

