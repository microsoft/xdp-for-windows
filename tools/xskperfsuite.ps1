#
# Runs a single queue XSK microbenchmark test suite on top of xskperf.ps1.
#

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64")]
    [string]$Arch = "x64",

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
    [switch]$RxInject = $false,

    [Parameter(Mandatory=$false)]
    [switch]$TxInspect = $false,

    [Parameter(Mandatory=$false)]
    [int]$TxInspectContentionCount = 1,

    [Parameter(Mandatory=$false)]
    [switch]$Fndis = $false,

    [Parameter(Mandatory=$false)]
    [int]$SocketCount = 1,

    [Parameter(Mandatory=$false)]
    [string]$RawResultsFile = "",

    [Parameter(Mandatory=$false)]
    [string]$XperfDirectory = "",

    [Parameter(Mandatory=$false)]
    [string]$CommitHash = ""
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

function ExtractKppsStat {
    param(
        $FileName
        )

    #
    # If multiple sockets are present, return the socket with the lowest Kpps.
    #
    $min = $null

    foreach ($s in Get-Content $FileName) {
        $index = $s.IndexOf("avg=")

        if ($index -lt 0) {
            continue
        }

        $s = $s.SubString($index + 4)
        $s = [double]$s.SubString(0, $s.IndexOf(" "))

        if ($min -eq $null) {
            $min = $s
        } elseif ($s -lt $min) {
            $min = $s
        }
    }

    return $min
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
#    @{ChunkSize="64";IoSize="64"}     # artificial min packet
    @{ChunkSize="2048";IoSize="64"}   # >= MTU chunks, mostly small packets
    @{ChunkSize="2048";IoSize="1514"}  # >= MTU chunks, mostly large packets
#    @{ChunkSize="65536";IoSize="64000"}  # GSO/GRO packets
)

if ($Iterations -lt 2) {
    Write-Warning "Not enough iterations to compute standard deviation"
}

if ($DeepInspection) {
    $UdpDstPort = 12345
} else {
    $UdpDstPort = 0
}

$RootDir = Split-Path $PSScriptRoot -Parent

try {
    if ($AdapterNames.Contains("XDPMP")) {
        $XdpmpPollProvider = "NDIS"

        if ($Fndis) {
            $XdpmpPollProvider = "FNDIS"

            Write-Verbose "installing fndis..."
            & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Arch $Arch
            Write-Verbose "installed fndis."
        }

        Write-Verbose "installing xdpmp..."
        & "$RootDir\tools\setup.ps1" -Install xdpmp -Config $Config -Arch $Arch -XdpmpPollProvider $XdpmpPollProvider
        Write-Verbose "installed xdpmp."
    }

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Arch $Arch
    Write-Verbose "installed xdp."

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
                        $XperfFile = $null

                        if ($RxInject) {
                            $Options += "-RXINJECT"
                        }
                        if ($TxInspect) {
                            $Options += "-TXINSPECT"
                        }
                        if ($Fndis) {
                            $Options += "-FNDIS"
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
                            $XperfFile = "$XperfDirectory\$ScenarioName.etl"
                        }

                        try {
                            for ($i = 0; $i -lt $Iterations; $i++) {
                                $TmpFile = [System.IO.Path]::GetTempFileName()
                                & $RootDir\tools\xskperf.ps1 `
                                    -AdapterName $AdapterName -Mode $Mode `
                                    -BufferSize $IoBufferPair.ChunkSize -BufferCount $XskNumBuffers `
                                    -IoSize $IoBufferPair.IoSize -BatchSize $XskBatchSize `
                                    -Wait:$Wait -Duration $Duration -OutFile $TmpFile `
                                    -UdpDstPort $UdpDstPort -XdpMode $XdpMode -LargePages:$LargePages `
                                    -RxInject:$RxInject -TxInspect:$TxInspect `
                                    -TxInspectContentionCount $TxInspectContentionCount `
                                    -SocketCount:$SocketCount -Fndis:$Fndis -Config $Config `
                                    -Arch $Arch -XperfFile $XperfFile

                                $kppsList += ExtractKppsStat $TmpFile
                            }

                            $avg = ($kppsList | Measure-Object -Average).Average
                            $stddev = MeasureStandardDeviation $kppsList
                            Write-Host $($Format -f $ScenarioName, [Math]::ceiling($avg), [Math]::ceiling($stddev))

                            if (-not [string]::IsNullOrEmpty($RawResultsFile)) {
                                Add-Content -Path $RawResultsFile -Value `
                                    ("{0},{1},{2},{3}" -f `
                                        $ScenarioName, `
                                        $CommitHash, `
                                        ([DateTimeOffset](Get-Date)).ToUnixTimeSeconds(), `
                                        ($kppsList -join ","))
                            }
                        } catch {
                            Write-Error "$($PSItem.Exception.Message)`n$($PSItem.ScriptStackTrace)"
                            Write-Host $($Format -f $ScenarioName, -1, -1)
                        }
                    }
                }
            }
        }
    }
} finally {
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    if ($AdapterNames.Contains("XDPMP")) {
        & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Arch $Arch -ErrorAction 'Continue'
        if ($Fndis) {
            & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Arch $Arch -ErrorAction 'Continue'
        }
    }
}
