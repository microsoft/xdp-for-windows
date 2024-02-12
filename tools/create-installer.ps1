#
# Build script for XDP installer.
#

param (
    [ValidateSet("x64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Config = "Debug"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$IsAdmin = Test-Admin
if (!$IsAdmin) {
    Write-Verbose "MSI validation requires admin privileges. Skipping."
}

msbuild.exe $RootDir\src\xdpinstaller\xdpinstaller.sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /p:IsAdmin=$IsAdmin
if (!$?) {
    Write-Error "Building the XDP installer failed: $LastExitCode"
}
