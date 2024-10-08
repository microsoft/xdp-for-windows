# Copyright (c) Microsoft Corporation
# Licensed under the MIT License.

param (
    [ValidateSet("x64", "arm64")]
    [Parameter(Mandatory=$false)]
    [string[]]$Platform = "x64",

    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Config = "Debug",

    [Parameter(Mandatory=$true)]
    [string]$InputFile = "",

    [Parameter(Mandatory=$true)]
    [string]$OutputFile = ""
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Commit = git.exe rev-parse HEAD
$VersionString = Get-XdpBuildVersionString
$content = Get-Content $InputFile
$content = $content.Replace("{commit}", $Commit)
$content = $content.Replace("{version}", $VersionString)
$content = $content.Replace("{version}", $VersionString)
$content = $content.Replace("{rootpath}", $RootDir)
$content = $content.Replace("{arch}", $Platform[0])

$ExpandedContent = @()

foreach ($Line in $content) {
    if ($Line -like "*{anyarch}*" -or $Line -like "*{binpath_anyarch}*") {
        foreach ($PlatformName in $Platform) {
            $PlatformLine = $Line.Replace("{anyarch}", $PlatformName)
            $PlatformLine = $PlatformLine.Replace("{binpath_anyarch}", $(Get-ArtifactBinPath -Platform $PlatformName -Config $Config))
            $ExpandedContent += $PlatformLine
        }
    } else {
        $ExpandedContent += $Line
    }
}

set-content $OutputFile $ExpandedContent
