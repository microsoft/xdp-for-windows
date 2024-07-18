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
    [switch]$NoInstaller = $false,

    [Parameter(Mandatory = $false)]
    [switch]$DevKit = $false,

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
    $NoInstaller = $true
}

$Sln = "$RootDir\xdp.sln"
if ($OneBranch) {
    $Sln = "$RootDir\xdp-onebranch.sln"
}

& $RootDir\tools\prepare-machine.ps1 -ForBuild -Force:$UpdateDeps

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

Write-Verbose "Restoring packages [xdpinstaller.sln]"
nuget.exe restore $RootDir\src\xdpinstaller\xdpinstaller.sln `
    -ConfigFile $RootDir\src\nuget.config

& $RootDir\tools\prepare-machine.ps1 -ForEbpfBuild

Write-Verbose "Building [$Sln]"
msbuild.exe $Sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /p:SignMode=TestSign `
    /t:$($Tasks -join ",") `
    /maxCpuCount
if (!$?) {
    Write-Error "Build failed: $LastExitCode"
}

if (!$NoInstaller) {
    & $RootDir\tools\create-installer.ps1 -Config $Config -Platform $Platform
}

if ($DevKit) {
    & $RootDir\tools\create-devkit.ps1 -Config $Config
}

if ($TestArchive) {
    & $RootDir\tools\create-test-archive.ps1 -Config $Config
}
