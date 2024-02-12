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

# msbuild.exe $RootDir\src\xdpinstaller\xdpinstaller.sln `
#     /t:restore `
#     /p:RestorePackagesConfig=true `
#     /p:RestoreConfigFile=src\nuget.config `
#     /p:Configuration=$Config `
#     /p:Platform=$Platform
# if (!$?) {
#     Write-Error "Restoring NuGet packages failed: $LastExitCode"
# }

nuget.exe restore $RootDir\src\xdpinstaller\xdpinstaller.sln `
    -ConfigFile $RootDir\src\nuget.config

msbuild.exe $RootDir\src\xdpinstaller\xdpinstaller.sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /p:IsAdmin=$IsAdmin
if (!$?) {
    Write-Error "Building the XDP installer failed: $LastExitCode"
}
