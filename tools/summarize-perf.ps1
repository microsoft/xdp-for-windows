param (
    [Parameter(Mandatory=$true)]
    [string[]]$Files
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Summary = @{}

# First, merge metrics from all runs, indexed by scenario name, platform, and commit hash.
foreach ($File in Get-Item -Path $Files) {
    $Data = New-PerfDataSet -Files $File

    foreach ($Run in $Data) {
        $Key = [ordered]@{
            ScenarioName = $Run.ScenarioName
            Platform = $Run.Platform
            CommitHash = $Run.CommitHash
        }
        $KeyString = $Key | Out-String

        if (!$Summary.ContainsKey($KeyString)) {
            $Summary[$KeyString] = @{
                Key = $Key
                Metrics = @{}
            }
        }

        foreach ($Metric in $Run.Metrics) {
            [array]$Summary[$KeyString].Metrics[$Metric.Name] += $Metric.Value
        }
    }
}

# Second, flatten the merged metrics and summarize the results across runs.
$Summary.Values | ForEach-Object {
    foreach ($Metric in $_.Metrics.GetEnumerator()) {
        [pscustomobject]@{
            ScenarioName = $_.Key.ScenarioName
            Platform = $_.Key.Platform
            CommitHash = $_.Key.CommitHash
            MetricName = $Metric.Key
            Average = $Metric.Value | Measure-Object -Average | Select-Object -ExpandProperty Average
            Minimum = $Metric.Value | Measure-Object -Minimum | Select-Object -ExpandProperty Minimum
            Maximum = $Metric.Value | Measure-Object -Maximum | Select-Object -ExpandProperty Maximum
        }
    }
} | Sort-Object -Property ScenarioName, Platform, CommitHash, MetricName
