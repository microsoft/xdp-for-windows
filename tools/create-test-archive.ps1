#
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
# Creates an archive of XDP test artifacts to validate compatibility with newer
# versions of XDP binaries.
# Code must be built before running this script.
#

param (
    [ValidateSet("x64", "arm64")]
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
$ArtifactBin = Get-ArtifactBinPath -Config $Config -Arch $Platform

$Name = "xdp-tests-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\testarchive\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

New-Item -Path $DstPath\bin -ItemType Directory > $null
New-Item -Path $DstPath\bin\test -ItemType Directory > $null
copy "$ArtifactBin\test\xdpfunctionaltests.dll" $DstPath\bin\test\

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "$ArtifactBin\test\xdpfunctionaltests.pdb" $DstPath\symbols

$VersionString = Get-XdpBuildVersionString

Compress-Archive -DestinationPath "$DstPath\$Name-$VersionString.zip" -Path $DstPath\*
