<#

.SYNOPSIS
Runs serial, deterministic XSK create/UMEM, RX, TX, and RX+TX bind/activate cases
on one XDPMP NIC. Reports conditional operation success, HRESULT histograms,
latency, and time buckets under the existing spinxsk injection configuration.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Platform
    The CPU architecture to use.

.PARAMETER Iterations
    Number of fresh-socket attempts per case per batch.

.PARAMETER Batches
    Number of repetitions of the complete case ladder.

.PARAMETER XdpmpPollProvider
    XDPMP poll provider, matching the stress job.

.PARAMETER EbpfPreinstalled
    eBPF is already installed (inbox); do not install/uninstall it here.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateRange(1, 100000)]
    [Int32]$Iterations = 300,

    [Parameter(Mandatory = $false)]
    [ValidateRange(1, 10)]
    [Int32]$Batches = 3,

    [Parameter(Mandatory = $false)]
    [ValidateSet("NDIS", "FNDIS")]
    [string]$XdpmpPollProvider = "FNDIS",

    [Parameter(Mandatory = $false)]
    [switch]$EbpfPreinstalled = $false,

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

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Forwarded = Invoke-XdpRemoteIfRequested -InvocationCommand $MyInvocation.MyCommand `
    -BoundParameters $PSBoundParameters -Config $Config -Platform $Platform
if ($Forwarded -is [array]) { $Forwarded = $Forwarded[-1] }
if ($Forwarded) { return }

$ArtifactsDir = Get-ArtifactBinPath -Config $Config -Platform $Platform
$FaultRate = "$ArtifactsDir\test\xdpfaultrate.exe"
$LogsDir = "$RootDir\artifacts\logs"
$TraceStarted = $false

if (!(Test-Path $FaultRate)) {
    Write-Error "$FaultRate does not exist!"
}

New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null
$OperatingSystem = Get-CimInstance Win32_OperatingSystem
$Metadata = [ordered]@{
    Utc = [DateTime]::UtcNow.ToString('o')
    OS = $OperatingSystem.Caption
    Version = $OperatingSystem.Version
    Build = $OperatingSystem.BuildNumber
    UptimeSeconds = ((Get-Date) - $OperatingSystem.LastBootUpTime).TotalSeconds
    FreePhysicalMemoryKB = $OperatingSystem.FreePhysicalMemory
    CPU = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors)
    Config = $Config
    Platform = $Platform
    PollProvider = $XdpmpPollProvider
    Iterations = $Iterations
    Batches = $Batches
    Files = @(Get-ChildItem $ArtifactsDir -Recurse -File |
        Where-Object { $_.Name -in @('xdp.sys', 'xdpmp.sys', 'fndis.sys', 'xdpfaultrate.exe') } |
        ForEach-Object { Get-FileHash $_.FullName -Algorithm SHA256 })
}
$Metadata | ConvertTo-Json -Depth 6 | Tee-Object -FilePath "$LogsDir\xdpfaultrate-environment.json"
verifier.exe /query | Tee-Object -FilePath "$LogsDir\xdpfaultrate-verifier-before.txt"
verifier.exe /querysettings | Tee-Object -FilePath "$LogsDir\xdpfaultrate-verifier-settings.txt"

try {
    & "$RootDir\tools\log.ps1" -Start -Name xdpfaultrate -Profile SpinXsk.Verbose -Config $Config -Platform $Platform
    $TraceStarted = $true
    if ($XdpmpPollProvider -eq "FNDIS") {
        & "$RootDir\tools\setup.ps1" -Install fndis -Config $Config -Platform $Platform
    }
    if (!$EbpfPreinstalled) {
        Write-Verbose "installing ebpf..."
        & "$RootDir\tools\setup.ps1" -Install ebpf -Config $Config -Platform $Platform
    }

    Write-Verbose "installing xdp..."
    & "$RootDir\tools\setup.ps1" -Install xdp -Config $Config -Platform $Platform -EnableEbpf

    & "$RootDir\tools\setup.ps1" -Install xdpmp -XdpmpPollProvider $XdpmpPollProvider -Config $Config -Platform $Platform
    Set-NetAdapterRss XDPMP -NumberOfReceiveQueues 2
    $Adapter = Get-NetAdapter XDPMP -ErrorAction Stop
    $Adapter | Select-Object Name, ifIndex, Status, InterfaceDescription, DriverVersion |
        ConvertTo-Json | Tee-Object -FilePath "$LogsDir\xdpfaultrate-adapter.json"

    # Enable XDP's internal fault injection, matching spinxsk.
    Write-Verbose "reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpFaultInject /d 1 /t REG_DWORD /f"
    reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpFaultInject /d 1 /t REG_DWORD /f | Write-Verbose
    if ($LastExitCode -ne 0) { throw "Unable to enable XdpFaultInject" }
    reg.exe query HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpFaultInject |
        Tee-Object -FilePath "$LogsDir\xdpfaultrate-injection.txt"

    $FailedBatches = 0
    for ($Batch = 1; $Batch -le $Batches; $Batch++) {
        Write-Host "=== xdpfaultrate batch=$Batch utc=$([DateTime]::UtcNow.ToString('o')) ==="
        & $FaultRate -Iterations $Iterations -IfIndex $Adapter.ifIndex -QueueId 0 |
            Tee-Object -FilePath "$LogsDir\xdpfaultrate-batch-$Batch.txt"
        if ($LastExitCode -ne 0) { $FailedBatches++ }
    }
    verifier.exe /query | Tee-Object -FilePath "$LogsDir\xdpfaultrate-verifier-after.txt"
    if ($FailedBatches -ne 0) {
        throw "$FailedBatches diagnostic batches failed (see per-stage results)"
    }
} finally {
    & "$RootDir\tools\setup.ps1" -Uninstall xdpmp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    & "$RootDir\tools\setup.ps1" -Uninstall xdp -Config $Config -Platform $Platform -ErrorAction 'Continue'
    if (!$EbpfPreinstalled) {
        & "$RootDir\tools\setup.ps1" -Uninstall ebpf -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
    if ($XdpmpPollProvider -eq "FNDIS") {
        & "$RootDir\tools\setup.ps1" -Uninstall fndis -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
    if ($TraceStarted) {
        & "$RootDir\tools\log.ps1" -Stop -Name xdpfaultrate -Config $Config -Platform $Platform -ErrorAction 'Continue'
    }
}
