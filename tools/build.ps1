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

    [Parameter(Mandatory = $false)]
    [switch]$NoClean = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoSign = $false,

    [Parameter(Mandatory = $false)]
    [switch]$DevKit = $false,

    [Parameter(Mandatory = $false)]
    [switch]$RuntimeKit = $false,

    [Parameter(Mandatory = $false)]
    [switch]$UpdateDeps = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$Tasks = @("Build")
if (!$NoClean) {
    $Tasks = @("Clean") + $Tasks
}

tools/prepare-machine.ps1 -ForBuild -Force:$UpdateDeps

msbuild.exe xdp.sln `
    /t:restore `
    /p:RestorePackagesConfig=true `
    /p:RestoreConfigFile=src\nuget.config `
    /p:Configuration=$Config `
    /p:Platform=$Platform
if (!$?) {
    Write-Verbose "Restoring NuGet packages failed: $LastExitCode"
    return
}

tools/prepare-machine.ps1 -ForEbpfBuild

msbuild.exe xdp.sln `
    /p:Configuration=$Config `
    /p:Platform=$Platform `
    /t:$($Tasks -join ",") `
    /maxCpuCount
if (!$?) {
    Write-Verbose "Build failed: $LastExitCode"
    return
}

if (!$NoSign) {
    tools/sign.ps1 -Config $Config -Arch $Platform
}

if ($DevKit) {
    tools/create-devkit.ps1 -Config $Config
}

if ($RuntimeKit) {
    tools/create-runtime-kit.ps1 -Config $Config
}

# Build the MSI installer
msbuild.exe xdp.sln /target:xdpinstaller:Rebuild `
    /p:Configuration=Installer-$Config `
    /p:Platform=$Platform
if (!$?) {
    Write-Verbose "Building the XDP installer failed: $LastExitCode"
    return
}
