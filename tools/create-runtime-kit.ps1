#
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
# Assembles an XDP runtime package.
# Code must be built before running this script.
#

param (
    [ValidateSet("x64")]
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Flavor = "Debug"
)

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Name = "xdp-runtime-$Platform"
if ($Flavor -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\kit\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

copy docs\usage.md $DstPath

New-Item -Path $DstPath\bin -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Flavor)\CoreNetSignRoot.cer" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Flavor)\xdp\xdp.inf" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Flavor)\xdp\xdp.sys" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Flavor)\xdp\xdp.cat" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Flavor)\xdp\xdpapi.dll" $DstPath\bin

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Flavor)\xdp.pdb"   $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Flavor)\xdpapi.pdb" $DstPath\symbols


[xml]$XdpVersion = Get-Content $RootDir\xdp.props
$Major = $XdpVersion.Project.PropertyGroup.XdpMajorVersion
$Minor = $XdpVersion.Project.PropertyGroup.XdpMinorVersion
$Patch = $XdpVersion.Project.PropertyGroup.XdpPatchVersion

$VersionString = "$Major.$Minor.$Patch"

if (!((Get-BuildBranch).StartsWith("release/"))) {
    $VersionString += "-prerelease+" + (git.exe describe --long --always --dirty --exclude=* --abbrev=8)
}

Compress-Archive -DestinationPath "$DstPath\$Name-$VersionString.zip" -Path $DstPath\*
