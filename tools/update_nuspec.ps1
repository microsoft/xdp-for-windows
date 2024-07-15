# Copyright (c) Microsoft Corporation

param ($InputFile, $OutputFile, $Arch, $Config)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Ver = Get-XdpBuildVersion
$content = Get-Content $InputFile
$content = $content.Replace("{version}", "$($Ver.Major).$($Ver.Minor).$($Ver.Patch)")
$content = $content.Replace("{binpath}", $(Get-ArtifactBinPath -Arch $Arch -Config $Config))
set-content $OutputFile $content
