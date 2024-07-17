#
# Build script for XDP project.
#

param (
    [ValidateSet("x64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Config = "Debug",

    [Parameter(Mandatory=$false)]
    [string]$Project = "",

    [Parameter(Mandatory = $false)]
    [switch]$NoClean = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoSign = $false,

    [Parameter(Mandatory = $false)]
    [switch]$DevKit = $false,

    [Parameter(Mandatory = $false)]
    [switch]$TestArchive = $false,

    [Parameter(Mandatory = $false)]
    [switch]$UpdateDeps = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Tasks = @()
if ([string]::IsNullOrEmpty($Project)) {
    $Tasks += "Build"

    if (!$NoClean) {
        $Tasks = @("Clean") + $Tasks
    }
} else {
    $Clean = ""
    if (!$NoClean) {
        $Clean = ":Rebuild"
    }
    $Tasks += "$Project$Clean"
    $NoSign = $true
}

& $RootDir\tools\prepare-machine.ps1 -ForBuild -Force:$UpdateDeps

$IsAdmin = Test-Admin
if (!$IsAdmin) {
    Write-Verbose "MSI installer validation requires admin privileges. Skipping."
}

Write-Verbose "Restoring packages [xdp.sln]"
msbuild.exe $RootDir\xdp.sln `
    /t:restore `
    /p:RestorePackagesConfig=true `
    /p:RestoreConfigFile=src\nuget.config `
    /p:Configuration=$Config `
    /p:Platform=$Platform
if (!$?) {
    Write-Error "Restoring NuGet packages failed: $LastExitCode"
}

& $RootDir\tools\prepare-machine.ps1 -ForEbpfBuild

msbuild.exe $RootDir\xdp.sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /p:IsAdmin=$IsAdmin `
    /t:$($Tasks -join ",") `
    /maxCpuCount
if (!$?) {
    Write-Error "Build failed: $LastExitCode"
}

if (!$NoSign) {
    & $RootDir\tools\sign.ps1 -Config $Config -Arch $Platform
}

if ($DevKit) {
    & $RootDir\tools\create-devkit.ps1 -Config $Config
}

if ($TestArchive) {
    & $RootDir\tools\create-test-archive.ps1 -Config $Config
}
