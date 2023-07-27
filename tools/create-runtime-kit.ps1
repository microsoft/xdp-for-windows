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
    [string]$Config = "Debug"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Name = "xdp-runtime-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\kit\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

copy docs\usage.md $DstPath

New-Item -Path $DstPath\bin -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\CoreNetSignRoot.cer" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Config)\xdpinstaller\xdp-for-windows.msi" $DstPath\bin

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\xdp.pdb"   $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\xdpapi.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\xdpcfg.pdb" $DstPath\symbols

[xml]$XdpVersion = Get-Content $RootDir\src\xdp.props
$Major = $XdpVersion.Project.PropertyGroup.XdpMajorVersion
$Minor = $XdpVersion.Project.PropertyGroup.XdpMinorVersion
$Patch = $XdpVersion.Project.PropertyGroup.XdpPatchVersion

$VersionString = "$Major.$Minor.$Patch"

if (!(Is-ReleaseBuild)) {
    $VersionString += "-prerelease+" + (git.exe describe --long --always --dirty --exclude=* --abbrev=8)
}

Compress-Archive -DestinationPath "$DstPath\$Name-$VersionString.zip" -Path $DstPath\*
