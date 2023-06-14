#
# Does a statistical comparison between two sets of output data from xskperfsuite.ps1.
#

param (
    [Parameter(Mandatory=$true)]
    [string]$DataFile1,

    [Parameter(Mandatory=$true)]
    [string]$DataFile2
)

function ImportDataset {
    param($File)

    $dataset = [ordered]@{}

    $contents = Get-Content $File

    foreach ($line in $contents) {
        $array = $line.Split(",")
        $scenarioName = $array[0]
        $scenarioData = $array[3..$array.Count]
        $dataset.Add($scenarioName, $scenarioData)
    }

    return $dataset
}

function MeasureVariance {
    param(
        $list
        )

    if ($list.Length -lt 2) {
        return -1
    }

    $var = 0
    $avg = ($list | Measure-Object -Average).Average
    foreach ($val in $list) {
        $var += [Math]::pow(($val - $avg), 2)
    }

    return $var / ($list.Length - 1)
}

# Two-sided, p = 0.05, indexed by (degrees of freedom - 1)
$criticalValues =
@(
    12.71, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262,
    2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2,110, 2.101,
    2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052
)

$dataset1 = ImportDataset $DataFile1
$dataset2 = ImportDataset $DataFile2

$Format = "{0,-50} {1,6} {2,6} {3,6} {4,20}"
Write-Host $($Format -f "Test Case", "Avg1", "Avg2", "%Diff", "Significance")
foreach ($scenarioName in $dataset1.Keys) {
    $data1 = $dataset1[$scenarioName]
    $data2 = $dataset2[$scenarioName]

    $x1 = [Math]::round(($data1 | Measure-Object -Average).Average)
    $x2 = [Math]::round(($data2 | Measure-Object -Average).Average)
    $pctDiff = [Math]::round((($x2 - $x1) / $x1) * 100)
    $s1 = MeasureVariance $data1
    $s2 = MeasureVariance $data2
    $n1 = $data1.Count
    $n2 = $data2.Count

    #
    # Welch's t-test
    #

    # t-test statistic
    $t = [Math]::abs(($x1 - $x2) / [Math]::sqrt(($s1 / $n1) + ($s2 / $n2)))

    # Degrees of freedom
    $df = [Math]::pow((($s1 / $n1) + ($s2 / $n2)), 2) / (([Math]::pow(($s1 / $n1), 2) / ($n1 - 1)) + ([Math]::pow(($s2 / $n2), 2) / ($n2 - 1)))
    $df = [Math]::floor($df)
    if ($df -gt $criticalValues.Count) {
        # Degrees of freedom exceeds our table. Cap the degrees of freedom for a
        # conservative approximation.
        $df = $criticalValues.Count
    }

    # Critical value
    $p = $criticalValues[$df - 1]

    if ($t -ge $p) {
        $result = "Significant"
    } else {
        $result = "NOT Significant"
    }


    Write-Verbose "x1:$x1 x2:$x2 s1:$s1 s2:$s2 n1:$n1 n2:$n2 t:$t df:$df p:$p"
    Write-Host $($Format -f $scenarioName, $x1, $x2, $pctDiff, $result)
}
