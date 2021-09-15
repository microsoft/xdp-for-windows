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
    [switch]$NoClean = $false
)

$Tasks = @("Build")
if (!$NoClean) {
    $Tasks = @("Clean") + $Tasks
}

msbuild.exe xdp.sln `
    /p:Configuration=$Flavor `
    /p:Platform=$Platform `
    /t:$($Tasks -join ",") `
    /maxCpuCount
