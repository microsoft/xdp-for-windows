# Copyright (c) Microsoft Corporation

param ($InputFile, $OutputFile, $Arch, $Config)

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
$content = $content.Replace("{rootpath}", $RootDir)
$content = $content.Replace("{binpath}", $(Get-ArtifactBinPath -Arch $Arch -Config $Config))
set-content $OutputFile $content
