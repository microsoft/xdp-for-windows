#
# Build script for XDP project.
#

param (
    [ValidateSet("x64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Flavor = "Debug",

    [Parameter(Mandatory = $false)]
    [switch]$NoClean = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoSign = $false,

    [Parameter(Mandatory = $false)]
    [switch]$UpdateDeps = $false
)

$Tasks = @("Build")
if (!$NoClean) {
    $Tasks = @("Clean") + $Tasks
}

tools/prepare-machine.ps1 -ForBuild -Force:$UpdateDeps

msbuild.exe xdp.sln `
    /p:Configuration=$Flavor `
    /p:Platform=$Platform `
    /t:$($Tasks -join ",") `
    /maxCpuCount

if (!$NoSign) {
    tools/sign.ps1 -Config $Flavor -Arch $Platform
}
