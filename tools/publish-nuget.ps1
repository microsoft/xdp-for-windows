#
# Publishes a local NuGet package file to a feed.
#

param (
    [Parameter(Mandatory=$true)]
    [string]$FeedUrl,

    [Parameter(Mandatory=$true)]
    [string]$PackagePath
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

nuget.exe push -Source $FeedUrl -ApiKey az $PackagePath
