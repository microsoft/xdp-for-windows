<#

.SYNOPSIS
This cleans up duplicate build artifacts for CI upload. Note that user mode
artifacts are left in the base directory.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Artifact paths.
$RootDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"

# Move files into the specific driver folders.
Move-Item "$ArtifactsDir\fndis.lib" "$ArtifactsDir\fndis\fndis.lib"

# Delete all unnecessary duplicates.
Remove-Item "$ArtifactsDir\fndis.sys"
Remove-Item "$ArtifactsDir\xdp.inf"
Remove-Item "$ArtifactsDir\xdp.sys"
Remove-Item "$ArtifactsDir\xdpfnmp.inf"
Remove-Item "$ArtifactsDir\xdpfnmp.sys"
Remove-Item "$ArtifactsDir\xdpmp.inf"
Remove-Item "$ArtifactsDir\xdpmp.sys"
Remove-Item "$ArtifactsDir\xdpmp\fndis.sys"
Remove-Item "$ArtifactsDir\xdpfnlwf.inf"
Remove-Item "$ArtifactsDir\xdpfnlwf.sys"
