#
# Runs a single queue XSK microbenchmark test suite on top of xskperf.ps1.
#

param (
    [Parameter(Mandatory=$false)]
    [string[]]$AdapterNames = @("XDPMP"),

    [Parameter(Mandatory=$false)]
    [string[]]$Modes = @("RX", "TX", "FWD"),

    [Parameter(Mandatory=$false)]
    [ValidateSet("System","Generic","Native", "Winsock", "RIO")]
    [string[]]$XdpModes = @("Native", "Generic"),

    [Parameter(Mandatory=$false)]
    [ValidateSet("Busy", "Wait")]
    [string[]]$WaitModes = @("Busy"),

    [Parameter(Mandatory=$false)]
    [int]$Iterations = 8,

    [Parameter(Mandatory=$false)]
    [int]$Duration = 6,

    [Parameter(Mandatory=$false)]
    [int]$XskNumBuffers = 512,

    [Parameter(Mandatory=$false)]
    [int]$XskBatchSize = 16,

    [Parameter(Mandatory=$false)]
    [switch]$DeepInspection = $false,

    [Parameter(Mandatory=$false)]
    [switch]$LargePages = $false,

    [Parameter(Mandatory=$false)]
    [switch]$XdpReceiveBatch = $false,

    [Parameter(Mandatory=$false)]
    [string]$RawResultsFile = "",

    [Parameter(Mandatory=$false)]
    [string]$XperfDirectory = ""
)

function ExtractKppsStat {
    param(
        $FileName
        )

    $s = Get-Content $FileName -Raw
    $s = $s.SubString($s.IndexOf("avg=") + 4)
    $s = $s.SubString(0, $s.IndexOf(" "))

    return $s
}

function MeasureStandardDeviation {
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

    return [Math]::sqrt($var / ($list.Length - 1))
}

$IoBufferPairs = @(
    @{ChunkSize="64";IoSize="64"}     # artificial min packet
    @{ChunkSize="2048";IoSize="64"}   # >= MTU chunks, mostly small packets
    @{ChunkSize="2048";IoSize="1514"}  # >= MTU chunks, mostly large packets
    @{ChunkSize="65536";IoSize="64000"}  # GSO/GRO packets
)

if ($Iterations -lt 2) {
    Write-Warning "Not enough iterations to compute standard deviation"
}

if ($DeepInspection) {
    $UdpDstPort = 12345
} else {
    $UdpDstPort = 0
}

$Format = "{0,-73} {1,-14} {2,-14}"
Write-Host $($Format -f "Test Case", "Avg (Kpps)", "Std Dev (Kpps)")
foreach ($AdapterName in $AdapterNames) {
    foreach ($XdpMode in $XdpModes) {
        foreach ($Mode in $Modes) {
            foreach ($WaitMode in $WaitModes) {
                foreach ($IoBufferPair in $IoBufferPairs) {
                    $kppsList = @()

                    $WaitMode = $WaitMode.ToUpper()
                    $Wait = $WaitMode -eq "WAIT"
                    $Options = ""

                    if ($XdpReceiveBatch) {
                        $Options += "-RXBATCH"
                    }

                    $ScenarioName = `
                        $AdapterName `
                        + "-" + $XdpMode.ToUpper() `
                        + "-" + $Mode `
                        + "-" + $WaitMode `
                        + "-" + $IoBufferPair.ChunkSize + "chunksize" `
                        + "-" + $IoBufferPair.IoSize + "iosize" `
                        + $Options

                    if (-not [string]::IsNullOrEmpty($XperfDirectory)) {
                        New-Item -ItemType "directory" -Path $XperfDirectory -Force | Out-Null
                        .\test\scripts\xdpxperf.ps1 -Start
                    }

                    try {
                        for ($i = 0; $i -lt $Iterations; $i++) {
                            $TmpFile = [System.IO.Path]::GetTempFileName()
                            & .\test\scripts\xskperf.ps1 `
                                -AdapterName $AdapterName -Mode $Mode `
                                -BufferSize $IoBufferPair.ChunkSize -BufferCount $XskNumBuffers `
                                -IoSize $IoBufferPair.IoSize -BatchSize $XskBatchSize `
                                -Wait:$Wait -Duration $Duration -OutFile $TmpFile `
                                -UdpDstPort $UdpDstPort -XdpMode $XdpMode -LargePages:$LargePages `
                                -XdpReceiveBatch:$XdpReceiveBatch

                            $kppsList += ExtractKppsStat $TmpFile
                        }

                        $avg = ($kppsList | Measure-Object -Average).Average
                        $stddev = MeasureStandardDeviation $kppsList
                        Write-Host $($Format -f $ScenarioName, [Math]::ceiling($avg), [Math]::ceiling($stddev))

                        if (-not [string]::IsNullOrEmpty($RawResultsFile)) {
                            Add-Content -Path $RawResultsFile -Value ("{0},{1}" -f $ScenarioName, ($kppsList -join ","))
                        }
                    } catch {
                        Write-Error "$($PSItem.Exception.Message)`n$($PSItem.ScriptStackTrace)"
                        Write-Host $($Format -f $ScenarioName, -1, -1)
                    } finally {
                        if (-not [string]::IsNullOrEmpty($XperfDirectory)) {
                            .\test\scripts\xdpxperf.ps1 -Stop -OutFile (Join-Path $XperfDirectory "$ScenarioName.etl")
                        }
                    }
                }
            }
        }
    }
}