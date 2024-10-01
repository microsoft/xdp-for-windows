#
# Build script for XDP project.
#

param (
    [ValidateSet("x64", "arm64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Config = "Debug",

    [Parameter(Mandatory=$false)]
    [string]$Project = "",

    [ValidateSet("", "Binary", "Catalog", "Package", "AllPackage")]
    [Parameter(Mandatory=$false)]
    [string]$BuildStage = "",

    [Parameter(Mandatory = $false)]
    [switch]$NoClean = $false,

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

$SignMode = "TestSign"
$Sln = "$RootDir\xdp.sln"

if ($OneBranch) {
    if (![string]::IsNullOrEmpty($Project)) {
        Write-Error "-OneBranch cannot be set with -Project"
    }

    $Project = "onebranch"
    $SignMode = "Off"
}

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

& $RootDir\tools\prepare-machine.ps1 -ForBuild -Force:$UpdateDeps -Platform $Platform

$IsAdmin = Test-Admin
if (!$IsAdmin) {
    Write-Verbose "MSI installer validation requires admin privileges. Skipping."
}

Write-Verbose "Restoring packages [$Sln]"
msbuild.exe $Sln `
    /t:restore `
    /p:RestoreConfigFile=src\nuget.config `
    /p:Configuration=$Config `
    /p:Platform=$Platform
if (!$?) {
    Write-Error "Restoring NuGet packages failed: $LastExitCode"
}

& $RootDir\tools\prepare-machine.ps1 -ForEbpfBuild -Platform $Platform

Write-Verbose "Building [$Sln]"
msbuild.exe $Sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /p:IsAdmin=$IsAdmin `
    /p:SignMode=$SignMode `
    /p:BuildStage=$BuildStage `
    /t:$($Tasks -join ",") `
    /maxCpuCount
if (!$?) {
    Write-Error "Build failed: $LastExitCode"
}

if ($TestArchive) {
    & $RootDir\tools\create-test-Archive.ps1 -Config $Config
}
