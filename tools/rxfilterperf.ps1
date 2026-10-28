param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("System", "Generic", "Native")]
    [string]$XdpMode = "System",

    [Parameter(Mandatory = $false)]
    [int]$QueueCount = 1,

    [Parameter(Mandatory = $false)]
    [int]$Timeout = 30,

    [Parameter(Mandatory=$false)]
    [switch]$Fndis = $false,

    [Parameter(Mandatory = $false)]
    [ValidateSet("Drop", "Pass", "L2Fwd")]
    [string]$Action = "Drop",

    [Parameter(Mandatory = $false)]
    [switch]$Ebpf,

    [Parameter(Mandatory = $false)]
    [switch]$EbpfPreinstalled,

    [Parameter(Mandatory=$false)]
    [string]$RawResultsFile = "",

    [Parameter(Mandatory=$false)]
    [string]$CommitHash = "",

    [Parameter(Mandatory = $false)]
    [string]$ComputerName = "",

    [Parameter(Mandatory = $false)]
    [System.Management.Automation.PSCredential]$Credential,

    [Parameter(Mandatory = $false)]
    [string]$RemoteRoot = "",

    [Parameter(Mandatory = $false)]
    [switch]$SkipDeploy
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Forwarded = Invoke-XdpRemoteIfRequested -InvocationCommand $MyInvocation.MyCommand `
    -BoundParameters $PSBoundParameters -Config $Config -Platform $Platform
if ($Forwarded -is [array]) { $Forwarded = $Forwarded[-1] }
if ($Forwarded) { return }
$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$LogsDir = "$RootDir\artifacts\logs"

$ScenarioName = "RXFILTER-$XdpMode-$Action-$($QueueCount)Q"
if ($Ebpf) {
    $RxFilter = "$ArtifactsDir\test\rxfilter.exe"
    switch ($Action) {
        "Drop"  { $ProgramName = "drop" }
        "Pass"  { $ProgramName = "pass" }
        "L2Fwd" { $ProgramName = "l1fwd" }
    }
    $BpfProgram = "$ArtifactsDir\test\bpf\$ProgramName.sys"
    if (!(Test-Path $BpfProgram)) {
        throw "$BpfProgram does not exist!"
    }
    $ScenarioName += "-EBPF"
} else {
    Write-Warning "rxfilterperf uses the deprecated built-in program API; pass -Ebpf to test eBPF."
    $RxFilter = "$ArtifactsDir\test\rxfilter-deprecated.exe"
}
if (!(Test-Path $RxFilter)) {
    throw "$RxFilter does not exist!"
}
$LogName = "rxfiltercpu_$ScenarioName"

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null
$RxFilterProcess = $null

$XdpmpPollProvider = "NDIS"
if ($Fndis) {
    $XdpmpPollProvider = "FNDIS"
}

try {
    if ($Ebpf -and !$EbpfPreinstalled) {
        & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Platform $Platform
    }
    if ($Ebpf) {
        Refresh-Path
    }

    if ($Fndis) {
        Write-Verbose "installing fndis..."
        & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Platform $Platform
        Write-Verbose "installed fndis."
    }

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform -EnableEbpf:$Ebpf
    Write-Verbose "installed xdp."
    if ($Ebpf) {
        Set-XdpEbpfMode -Mode $XdpMode
    }

    Write-Verbose "installing xdpmp..."
    & "$RootDir\tools\setup.ps1" -Install xdpmp -XdpmpPollProvider $XdpmpPollProvider -Config $Config -Platform $Platform
    Write-Verbose "installed xdpmp."

    Write-Verbose "Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount"
    Set-NetAdapterRss XDPMP -NumberOfReceiveQueues $QueueCount

    & "$RootDir\tools\log.ps1" -Start -Name $LogName -Profile CpuSample.Verbose -Config $Config -Platform $Platform

    $ArgList = @("-IfIndex", (Get-NetAdapter -Name XDPMP).ifIndex)
    if ($Ebpf) {
        $ArgList += @("-BpfProgram", "`"$BpfProgram`"", "-ProgramName", $ProgramName)
    } else {
        $ArgList += @("-QueueId", "*", "-MatchType", "All", "-Action", $Action, "-XdpMode", $XdpMode)
    }
    Write-Verbose "$RxFilter $ArgList"
    $OutputFile = "$LogsDir\$LogName.stdout.txt"
    $ErrorFile = "$LogsDir\$LogName.stderr.txt"
    $RxFilterProcess = Start-Process $RxFilter -PassThru -ArgumentList $ArgList `
        -RedirectStandardOutput $OutputFile -RedirectStandardError $ErrorFile

    $ReadyTimeout = [Diagnostics.Stopwatch]::StartNew()
    while (!(Select-String -Path $OutputFile -SimpleMatch "attached" -Quiet)) {
        if ($RxFilterProcess.HasExited) {
            throw "rxfilter exited with code $($RxFilterProcess.ExitCode): $(Get-Content $ErrorFile -Raw)"
        }
        if ($ReadyTimeout.Elapsed.TotalSeconds -ge 60) {
            throw "Timed out waiting for rxfilter to attach: $(Get-Content $ErrorFile -Raw)"
        }
        Start-Sleep -Milliseconds 100
    }

    & $RootDir\tools\xdpmpratesim.ps1 -AdapterName XDPMP -Unlimited

    Write-Verbose "Start XDP counters:"
    Get-Counter ((Get-Counter -ListSet XDP*).Paths) -ErrorAction Ignore | Out-String | Write-Verbose

    $StartPackets = (Get-NetAdapterStatistics -Name XDPMP).ReceivedUnicastPackets
    Start-Sleep -Seconds $Timeout
    $EndPackets = (Get-NetAdapterStatistics -Name XDPMP).ReceivedUnicastPackets

    if ($RxFilterProcess.HasExited) {
        throw "rxfilter exited unexpectedly with code $($RxFilterProcess.ExitCode)"
    }
    if ($EndPackets -le $StartPackets) {
        throw "No packets were processed"
    }

    Write-Verbose "End XDP counters:"
    Get-Counter ((Get-Counter -ListSet XDP*).Paths) -ErrorAction Ignore | Out-String | Write-Verbose

    & $RootDir\tools\xdpmpratesim.ps1 -AdapterName XDPMP -RxFramesPerInterval 1000

    $Kpps = ($EndPackets - $StartPackets) / 1000 / $Timeout
    Write-Output "Filtered $(($EndPackets - $StartPackets)) packets in $Timeout seconds ($Kpps Kpps)."

    if (![string]::IsNullOrEmpty($RawResultsFile)) {
        $Results = New-PerfDataSet -Files $RawResultsFile

        [void]$Results.Add($(
            New-PerfData -ScenarioName $ScenarioName `
                -Platform $Platform -CommitHash $CommitHash -Metrics @(
                    @{
                        Name = "pps"
                        Value = $Kpps
                        Scale = 1000
                    }
                )
            )
        )

        Write-PerfDataSet -DataSet $Results -File $RawResultsFile
    }
} finally {
    if ($RxFilterProcess -and !$RxFilterProcess.HasExited) {
        Stop-Process -Id $RxFilterProcess.Id
    }

    & "$RootDir\tools\log.ps1" -Stop -Name $LogName -Config $Config -Platform $Platform -ErrorAction 'Continue'

    if ($Ebpf) {
        Set-XdpEbpfMode -Mode System -ErrorAction Continue
    }
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    if ($Ebpf -and !$EbpfPreinstalled) {
        & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
    if ($Fndis) {
        & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
}
