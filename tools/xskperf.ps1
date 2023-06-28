#
# Runs a single queue XSK microbenchmark. For repeatable tests, the XSK
# application is pinned to CPU 0 and the XDP driver is pinned to CPU 2.
# (CPU 1 is often a shared core.)
#
# Generic XDP over DuoNIC dependencies:
# - 8 or more logical processors
#

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory=$true)]
    [string]$AdapterName,

    [ValidateSet("RX", "TX", "FWD")]
    [Parameter(Mandatory=$true)]
    [string]$Mode,

    [ValidateSet("System", "Generic", "Native", "Winsock", "RIO")]
    [Parameter(Mandatory=$false)]
    [string]$XdpMode = "System",

    [Parameter(Mandatory=$false)]
    [int]$BufferSize = 64,

    [Parameter(Mandatory=$false)]
    [int]$BufferCount = 256,

    [Parameter(Mandatory=$false)]
    [int]$IoSize = 64,

    [Parameter(Mandatory=$false)]
    [int]$BatchSize = 16,

    [Parameter(Mandatory=$false)]
    [int]$Duration = 5,

    [Parameter(Mandatory=$false)]
    [int]$XskCpu = 0,

    [Parameter(Mandatory=$false)]
    [int]$XskGroup = 0,

    [Parameter(Mandatory=$false)]
    [int]$XdpCpu = 2,

    [Parameter(Mandatory=$false)]
    [switch]$Wait = $false,

    [Parameter(Mandatory=$false)]
    [int]$UdpDstPort = 0,

    [Parameter(Mandatory=$false)]
    [switch]$Stats = $false,

    [Parameter(Mandatory=$false)]
    [switch]$RateSimulation = $false,

    [Parameter(Mandatory=$false)]
    [switch]$LargePages = $false,

    [Parameter(Mandatory=$false)]
    [switch]$RxInject = $false,

    [Parameter(Mandatory=$false)]
    [switch]$TxInspect = $false,

    [Parameter(Mandatory=$false)]
    [int]$TxInspectContentionCount = 1,

    [ValidateSet("System", "Busy", "Socket")]
    [Parameter(Mandatory=$false)]
    [string]$PollMode = "System",

    [Parameter(Mandatory=$false)]
    [switch]$Fndis = $false,

    [Parameter(Mandatory=$false)]
    [int]$SocketCount = 1,

    [Parameter(Mandatory=$false)]
    [int]$YieldCount = 0,

    [Parameter(Mandatory=$false)]
    [switch]$Pacing = $false,

    [Parameter(Mandatory=$false)]
    [string]$OutFile = "",

    [Parameter(Mandatory=$false)]
    [string]$XperfFile = ""
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

function Wait-NetAdapterUpStatus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)] [String] $Name,
        [ValidateRange(0, [Int]::MaxValue)] [Int] $Timeout = 60
    )

    $Timer = 0
    do {
        $Status = (Get-NetAdapter -Name $Name -ErrorAction "SilentlyContinue").Status
        if ($Status -eq "Up") {
            break
        }

        Start-Sleep 1
        $Timer++
    } until ($Timer -gt $Timeout)

    if ($Timer -gt $Timeout) {
        if ([String]::IsNullOrEmpty($Status)) {
            $Status = "<Does not exist>"
        }
        Write-Error "NIC $Name failed to be Up after $Timeout seconds. Final status is $Status."
    } else {
        Write-Verbose "NIC $Name is Up. Waited $Timer out of $Timeout seconds."
    }
}

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$WsaRio = Get-CoreNetCiArtifactPath -Name "wsario.exe"
$Mode = $Mode.ToUpper()
$Adapter = Get-NetAdapter $AdapterName
$AdapterIndex = $Adapter.ifIndex
$XskAffinity = 1 -shl $XskCpu
$UdpSize = $IoSize - 8 - 20 - 14

if ($UdpDstPort -eq 0 -and @("Winsock", "RIO").Contains($XdpMode)) {
    Write-Verbose "Using implicit UDP dest port 1234 for Winsock/RIO"
    $UdpDstPort = 1234
}

if ($SocketCount -ne 1 -and $Mode -ne "TX") {
    Write-Error "SocketCount is currently supported only in TX mode"
}

$AdapterRss = Get-NetAdapterRss -Name $AdapterName
if ($AdapterRss.BaseProcessorGroup -ne 0 -or
    $AdapterRss.BaseProcessorNumber -ne $XdpCpu -or
    $AdapterRss.MaxProcessorGroup -ne 0 -or
    $AdapterRss.MaxProcessorNumber -ne $XdpCpu -or
    $AdapterRss.NumberOfReceiveQueues -ne 1 -or
    $AdapterRss.Profile -ne "ClosestStatic") {
    Write-Verbose "Setting $AdapterName RSS affinity to $XdpCpu"
    $AdapterRss | Set-NetAdapterRss -BaseProcessorGroup 0 -BaseProcessorNumber $XdpCpu `
        -MaxProcessorGroup 0 -MaxProcessorNumber $XdpCpu -NumberOfReceiveQueues 1 `
        -Profile ClosestStatic
    $NicReset = $true
}

Write-Verbose "Restarting XDP"
Restart-Service xdp

try {
    $WsaRioProcess = $null

    #
    # Configure XDPMP.
    #
    if ($RateSimulation -and $Adapter.InterfaceDescription -notlike "XDPMP*") {
        Write-Error "Rate simulation is supported only by XDPMP" -ErrorAction Stop
    }
    if ($Adapter.InterfaceDescription -like "XDPMP*") {
        if (@("RX", "FWD").Contains($Mode) -and -not $TxInspect) {
            Write-Verbose "Setting XDPMP RX buffer size to $BufferSize"
            Set-NetAdapterAdvancedProperty -Name $AdapterName -RegistryKeyword RxBufferLength -RegistryValue $BufferSize -NoRestart
        }

        if (@("RX", "FWD").Contains($Mode) -and -not $TxInspect) {
            Write-Verbose "Setting XDPMP RX data length to $IoSize"
            Set-NetAdapterAdvancedProperty -Name $AdapterName -RegistryKeyword RxDataLength -RegistryValue $IoSize -NoRestart
        }

        if ($UdpDstPort -ne 0 -and -not $TxInspect) {
            $ArgList =
                "udp 22-22-22-22-00-02 22-22-22-22-00-00 192.168.100.2 192.168.100.1 1234 " +
                "$UdpDstPort $UdpSize"
            Write-Verbose "pktcmd.exe $ArgList"
            $UdpPattern = & $ArtifactsDir\pktcmd.exe $ArgList.Split(" ")

            # Since the packet data is set to zero in pktcmd and XDPMP
            # implicitly sets trailing packet data to zero, truncate the string
            # to fit within the max NDIS string length.
            if ($UdpPattern.Length -gt 150) {
                $UdpPattern = $UdpPattern.Substring(0, 150)
            }
        } else {
            $UdpPattern = $null
        }

        if (((Get-NetAdapterAdvancedProperty $AdapterName -RegistryKeyword RxPattern).RegistryValue) -ne $UdpPattern) {
            if ($UdpPattern -eq $null) {
                Write-Verbose "Clearing XDPMP RX pattern"
                Remove-NetAdapterAdvancedProperty $AdapterName -RegistryKeyword RxPattern -NoRestart
            } else {
                Write-Verbose "Setting XDPMP RX pattern to $UdpPattern"
                Set-NetAdapterAdvancedProperty $AdapterName -RegistryKeyword RxPattern -RegistryValue $UdpPattern -NoRestart
            }
        }

        if ($Fndis) {
            $PollProvider = "FNDIS"
        } else {
            $PollProvider = "NDIS"
        }

        Write-Verbose "Setting XDPMP poll provider to $PollProvider"
        Set-NetAdapterAdvancedProperty -Name $AdapterName -RegistryKeyword PollProvider -DisplayValue $PollProvider

        Write-Verbose "Restarting $AdapterName"
        Restart-NetAdapter $AdapterName
        Wait-NetAdapterUpStatus -Name $AdapterName

        $RxSimRate = 1
        $TxSimRate = 1
        if (@("RX", "FWD").Contains($Mode) -and -not $TxInspect) {
            $RxSimRate = 0xFFFFFFFFl
            if ($Pacing) {
                $RxSimRate = 1000
            }
        }
        if ((@("TX", "FWD").Contains($Mode) -and -not $RxInject) -or
            ($Mode -eq "RX" -and $TxInspect)) {
            $TxSimRate = 0xFFFFFFFFl
            if ($Pacing) {
                $TxSimRate = 1000
            }
        }

        Write-Verbose "Setting XDPMP rate simulation to RX:$RxSimRate TX:$TxSimRate"
        & $RootDir\tools\xdpmpratesim.ps1 -AdapterName $AdapterName `
            -RxFramesPerInterval $RxSimRate -TxFramesPerInterval $TxSimRate
    }

    #
    # If using generic XDP over duonic, generate RX load using the paired NIC.
    #
    if ($AdapterName -eq "duo1" -and @("RX", "FWD").Contains($Mode)) {
        if (((Get-NetAdapterAdvancedProperty duo* -DisplayName linkprocindex).DisplayValue) -ne ($XdpCpu + 4)) {
            Write-Verbose "Setting DuoNIC link thread affinity to $($XdpCpu + 4)"
            Set-NetAdapterAdvancedProperty duo* -DisplayName linkprocindex -RegistryValue ($XdpCpu + 4)
            $NicReset = $true
        }

        $DuoRss = Get-NetAdapterRss -Name "duo2"
        if ($DuoRss.BaseProcessorGroup -ne 0 -or
            $DuoRss.BaseProcessorNumber -ne ($XdpCpu + 2) -or
            $DuoRss.MaxProcessorGroup -ne 0 -or
            $DuoRss.MaxProcessorNumber -ne ($XdpCpu + 2) -or
            $DuoRss.Profile -ne "ClosestStatic") {
            Write-Verbose "Setting duo2 RSS affinity to $($XdpCpu + 2)"
            $DuoRss | Set-NetAdapterRss -BaseProcessorGroup 0 -BaseProcessorNumber ($XdpCpu + 2) `
                -MaxProcessorGroup 0 -MaxProcessorNumber ($XdpCpu + 2) -Profile ClosestStatic
            $NicReset = $true
        }

        if ($NicReset) {
            Write-Verbose "Waiting for IP address DAD to complete..."
            Start-Sleep -Seconds 5
        }

        $WsaRioIoSize = 32 * $UdpSize
        $WsaRioCpu = $XdpCpu + 5
        $ArgList =
            "Winsock Send -Bind 192.168.100.2:1234 -Target 192.168.100.1:1234 -Group $XskGroup " +
            "-CPU $WsaRioCpu -IoSize $WsaRioIoSize -Uso $UdpSize -IoCount -1"
        Write-Verbose "$WsaRio $ArgList"
        $WsaRioProcess = Start-Process $WsaRio -PassThru -ArgumentList $ArgList
    }
    #
    # If using TxInspect, generate TX load.
    #
    if ($TxInspect) {
        if ($XdpMode -ne "Generic") {
            Write-Error "TxInspect only supported in Generic mode"
        }

        if (@("RX", "FWD").Contains($Mode)) {
            $WsaRioCpu = $XdpCpu + 5
            $ArgList =
                "Winsock Send -Target 192.168.100.2:1234 -Group $XskGroup -CPU $WsaRioCpu " +
                "-IoSize $UdpSize -IoCount -1 -ThreadCount $TxInspectContentionCount"
            Write-Verbose "$WsaRio $ArgList"
            $WsaRioProcess = Start-Process $WsaRio -PassThru -ArgumentList $ArgList
        }
    }

    $ThreadParams = @()
    $QueueParams = @()
    $GlobalParams = @()

    if ($Wait) {
        $ThreadParams += " -w"
    }

    if ($YieldCount -ne 0) {
        $ThreadParams += " -yield $YieldCount"
    }

    if ($Stats) {
        $QueueParams += " -s"
    }

    if ($LargePages) {
        $GlobalParams += " -lp"
    }

    if ($RxInject) {
        $QueueParams += " -rx_inject"
    }

    if ($TxInspect) {
        $QueueParams += " -tx_inspect"
    }

    if (-not [string]::IsNullOrEmpty($XperfFile)) {
        & $RootDir\tools\log.ps1 -Start -Name xskcpu -Profile CpuSample.Verbose `
            -Config $Config -Arch $Arch
    }

    if (@("System", "Generic", "Native").Contains($XdpMode)) {
        $QueueParams =
            "-q -id 0 -c $BufferSize " +
            "-u $($BufferCount * $BufferSize) -b $BatchSize -txio $IoSize " +
            "-poll $PollMode -xdp_mode $XdpMode $QueueParams"
        $ArgList =
            "$Mode -i $AdapterIndex -p $UdpDstPort -d $Duration $GlobalParams " +
            "-t -ca $XskAffinity -group $XskGroup $ThreadParams " +
            (($QueueParams + " ") * $SocketCount)

        Write-Verbose "xskbench.exe $ArgList"
        if ([string]::IsNullOrEmpty($OutFile)) {
            Start-Process $ArtifactsDir\xskbench.exe -Wait -NoNewWindow $ArgList
        } else {
            $StdErrFile = [System.IO.Path]::GetTempFileName()
            Start-Process $ArtifactsDir\xskbench.exe -Wait -RedirectStandardOutput $OutFile `
                -RedirectStandardError $StdErrFile $ArgList
            $StdErr = Get-Content $StdErrFile
            if (-not [string]::IsNullOrWhiteSpace($StdErr)) {
                throw "xskbench.exe $StdErr"
            }
        }
    } else {
        if ($Mode -eq "RX") {
            $IoDirection = "Receive"
            Write-Verbose "Disabling URO"
            netsh.exe int udp set global uro=disabled | Out-Null
        } elseif ($Mode -eq "TX") {
            $IoDirection = "Send"
        } else {
            throw "Unsupported $XdpMode mode: $Mode"
        }

        if ($XdpMode -eq "Winsock") {
            $OptFlags = "0x1"
        } elseif ($XdpMode -eq "RIO") {
            $OptFlags = "0x5"
        }

        if ($Adapter.InterfaceDescription -like "XDPMP*") {
            $RemoteAddress = "192.168.100.2:1234"
        }

        $ArgList =
            "$XdpMode $IoDirection -Bind 0.0.0.0:$UdpDstPort -Target $RemoteAddress -Group $XskGroup " +
            "-CPU $XskCpu -IoSize $UdpSize -IoCount -1 -MaxDuration $Duration -OptFlags $OptFlags"
        Write-Verbose "$WsaRio $ArgList"
        $WsaRioOutput = & $WsaRio $ArgList.Split(" ")
        $WsaRioJson = $WsaRioOutput | ConvertFrom-Json
        $AvgKpps = [int]($WsaRioJson.Summary.IoCount / $WsaRioJson.Summary.ElapsedSeconds / 1000);

        if ([string]::IsNullOrEmpty($OutFile)) {
            Write-Host $AvgKpps
        } else {
            Set-Content -Path $OutFile -Value "avg=$AvgKpps "
        }
    }

} finally {

    if (-not [string]::IsNullOrEmpty($XperfFile)) {
        & $RootDir\tools\log.ps1 -Stop -Name xskcpu -Config $Config -Arch $Arch `
            -EtlPath $XperfFile
    }

    if ($WsaRioProcess) {
        Stop-Process -InputObject $WsaRioProcess
    }

    if ($Adapter.InterfaceDescription -like "XDPMP*") {
        Write-Verbose "Stopping XDPMP rate simulation"
        & $RootDir\tools\xdpmpratesim.ps1 -AdapterName $AdapterName `
            -RxFramesPerInterval 1 -TxFramesPerInterval 0xFFFFFFFFl
    }
}
