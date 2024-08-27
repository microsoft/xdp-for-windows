<#

.SYNOPSIS
This script merges the contents of an arbitrary directory into the binary
artifacts directory.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER SourcePath
    The files to merge into the artifacts directory.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $true)]
    [string]$SourcePath
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Arch $Arch

Copy-Item -Path $SourcePath -Destination $ArtifactsDir
