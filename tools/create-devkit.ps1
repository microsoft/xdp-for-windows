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
    [string]$Flavor = "Debug"
)

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Name = "xdp-devkit-$Platform"
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
copy "artifacts\bin\$($Platform)_$($Flavor)\pktcmd.exe" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Flavor)\rxfilter.exe" $DstPath\bin
copy "artifacts\bin\$($Platform)_$($Flavor)\xskbench.exe" $DstPath\bin

New-Item -Path $DstPath\symbols -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Flavor)\xdp.pdb"   $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Flavor)\xdpapi.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Flavor)\pktcmd.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Flavor)\rxfilter.pdb" $DstPath\symbols
copy "artifacts\bin\$($Platform)_$($Flavor)\xskbench.pdb" $DstPath\symbols

New-Item -Path $DstPath\include -ItemType Directory > $null
copy -Recurse published\external\* $DstPath\include

New-Item -Path $DstPath\lib -ItemType Directory > $null
copy "artifacts\bin\$($Platform)_$($Flavor)\xdpapi.lib" $DstPath\lib
copy "artifacts\bin\$($Platform)_$($Flavor)\xdpnmr.lib" $DstPath\lib
# Package the NMR symbols alongside its static library: consuming projects will
# throw build exceptions if symbols are missing for statically linked code.
copy "artifacts\bin\$($Platform)_$($Flavor)\xdpnmr.pdb" $DstPath\lib

[xml]$XdpVersion = Get-Content $RootDir\xdp.props
$Major = $XdpVersion.Project.PropertyGroup.XdpMajorVersion
$Minor = $XdpVersion.Project.PropertyGroup.XdpMinorVersion
$Patch = $XdpVersion.Project.PropertyGroup.XdpPatchVersion

$VersionString = "$Major.$Minor.$Patch"

if (!((Get-BuildBranch).StartsWith("release/"))) {
    $VersionString += "-prerelease+" + (git.exe describe --long --always --dirty --exclude=* --abbrev=8)
}

Compress-Archive -DestinationPath "$DstPath\$Name-$VersionString.zip" -Path $DstPath\*
