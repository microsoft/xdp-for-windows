param (
    [Parameter(Mandatory=$true)]
    [string[]]$Files
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

foreach ($File in $Files) {
    $Data = New-PerfDataSet -Files $File

    foreach ($Scenario in $Data) {
        $Metrics = $Scenario.Metrics | ForEach-Object { "$($_.Name)=$($_.Value)" }
        Write-Output "$($Scenario.ScenarioName) ($($Scenario.Platform)) $Metrics"
    }
}
