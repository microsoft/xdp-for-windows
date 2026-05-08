# This file returns the version information to internal ES systems.

param (
    [ValidateSet("Major", "Minor", "Patch")]
    [Parameter(Mandatory=$true)]
    [string]$Version = ""
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$XdpVersion = Get-XdpBuildVersion

switch ($Version) {
    "Major" { return $XdpVersion.Major }
    "Minor" { return $XdpVersion.Minor }
    "Patch" { return $XdpVersion.Patch }
}
