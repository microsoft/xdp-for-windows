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
    [switch]$NoInstaller = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoInstallerProjectReferences = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoDevNuget = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoDevNugetProjectReferences = $false,

    [Parameter(Mandatory = $false)]
    [switch]$TestArchive = $false,

    [Parameter(Mandatory = $false)]
    [switch]$UpdateDeps = $false,

    [Parameter(Mandatory = $false)]
    [switch]$OneBranch = $false
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
}

$SignMode = "TestSign"
if ($NoSign) {
    $SignMode = "Off"
}

$Sln = "$RootDir\xdp.sln"
if ($OneBranch) {
    $Sln = "$RootDir\xdp-onebranch.sln"
}

& $RootDir\tools\prepare-machine.ps1 -ForBuild -Force:$UpdateDeps

$IsAdmin = Test-Admin
if (!$IsAdmin) {
    Write-Verbose "MSI installer validation requires admin privileges. Skipping."
}

Write-Verbose "Restoring packages [$Sln]"
msbuild.exe $Sln `
    /t:restore `
    /p:RestorePackagesConfig=true `
    /p:RestoreConfigFile=src\nuget.config `
    /p:Configuration=$Config `
    /p:Platform=$Platform
if (!$?) {
    Write-Error "Restoring NuGet packages failed: $LastExitCode"
}

& $RootDir\tools\prepare-machine.ps1 -ForEbpfBuild

Write-Verbose "Building [$Sln]"
msbuild.exe $Sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /p:InstallerProjectReferences=$(!$NoInstallerProjectReferences) `
    /p:NoInstaller=$NoInstaller `
    /p:DevNugetProjectReferences=$(!$NoDevNugetProjectReferences) `
    /p:NoDevNuget=$NoDevNuget `
    /p:IsAdmin=$IsAdmin `
    /p:SignMode=$SignMode `
    /t:$($Tasks -join ",") `
    /maxCpuCount
if (!$?) {
    Write-Error "Build failed: $LastExitCode"
}

if ($TestArchive) {
    & $RootDir\tools\create-test-archive.ps1 -Config $Config
}
