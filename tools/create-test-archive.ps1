#
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
# Creates an archive of XDP test artifacts to validate compatibility with newer
# versions of XDP binaries.
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

$Name = "xdp-tests-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\tests\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

New-Item -Path $DstPath\bin\test -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\xdpfunctionaltests.dll" $DstPath\bin\test

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\xdpfunctionaltests.pdb"   $DstPath\symbols

$VersionString = Get-XdpBuildVersionString

if (!(Is-ReleaseBuild)) {
    $VersionString += "-prerelease+" + (git.exe describe --long --always --dirty --exclude=* --abbrev=8)
}

Compress-Archive -DestinationPath "$DstPath\$Name-$VersionString.zip" -Path $DstPath\*
