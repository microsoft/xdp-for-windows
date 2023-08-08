#
# Assembles a dev kit for both AF_XDP client and XDP driver development.
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

$Name = "xdp-devkit-$Platform"
if ($Config -eq "Debug") {
    $Name += "-debug"
}
$DstPath = "artifacts\kit\$Name"

Remove-Item $DstPath -Recurse -ErrorAction Ignore
New-Item -Path $DstPath -ItemType Directory > $null

copy docs\usage.md $DstPath

New-Item -Path $DstPath\bin -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\pktcmd.exe" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Config)\rxfilter.exe" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Config)\xdpcfg.exe" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Config)\xskbench.exe" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Config)\xskfwd.exe" $DstPath\bin

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\xdp.pdb"   $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\xdpapi.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\pktcmd.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\rxfilter.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\xdpcfg.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\xskbench.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Config)\xskfwd.pdb" $DstPath\symbols

New-Item -Path $DstPath\include -ItemType Directory > $null
copy -Recurse published\external\* $DstPath\include

New-Item -Path $DstPath\lib -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Config)\xdpapi.lib" $DstPath\lib
copy "artifacts\bin\$($Platform)_$($Config)\xdpnmr.lib" $DstPath\lib
# Package the NMR symbols alongside its static library: consuming projects will
# throw build exceptions if symbols are missing for statically linked code.
copy "artifacts\bin\$($Platform)_$($Config)\xdpnmr.pdb" $DstPath\lib

$VersionString = Get-XdpBuildVersionString

if (!(Is-ReleaseBuild)) {
    $VersionString += "-prerelease+" + (git.exe describe --long --always --dirty --exclude=* --abbrev=8)

    copy "artifacts\bin\$($Platform)_$($Config)\CoreNetSignRoot.cer" $DstPath\bin
}

Compress-Archive -DestinationPath "$DstPath\$Name-$VersionString.zip" -Path $DstPath\*
