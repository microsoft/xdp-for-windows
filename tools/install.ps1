# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
<#
.Synopsis
    Install (or uninstall) the latest version XDP on Windows.
.Parameter Uninstall
    Uninstall XDP if installed. If not set, install XDP.
.EXAMPLE
    Invoke this script directly from the web
    iex "& { $(irm https://aka.ms/xdp-install) }"
#>
param(
    [Parameter(Mandatory = $false)]
    [switch] $Uninstall
)

#Requires -RunAsAdministrator

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (!$Uninstall) {
    $Version = "1.0.0" # TODO: automatically determine the latest release version
    $Installer = "xdp-for-windows.$Version.msi"
    $URL = "https://github.com/microsoft/xdp-for-windows/releases/download/v$Version/$Installer"

    # Download the msi.
    Write-Host "Downloading $URL"
    Invoke-WebRequest -Uri $URL -OutFile $Installer

    # Install the XDP driver and cleanup the installer.
    Write-Host "Installing XDP v$Version"
    msiexec.exe /i $Installer /quiet | Out-Null
    Remove-Item $Installer
} else {
    Write-Output "Looking for installed XDP driver"
    $InstallId = (Get-CimInstance Win32_Product -Filter "Name = 'XDP for Windows'").IdentifyingNumber
    if ($InstallId) {
        # Uninstall the XDP driver and delete the folder.
        Write-Output "Uninstalling XDP driver"
        try { msiexec.exe /x $InstallId /quiet } catch { }
    }
}