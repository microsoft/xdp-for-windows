<#

.SYNOPSIS
This script runs spinxsk.exe over and over until the number of specified minutes
have elapsed or a failure is seen. The tool is run multiple times in short iterations
rather than for one long iteration to keep the size of traces down and to provide
more coverage for setup and cleanup.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER Minutes
    Duration of execution in minutes. If 0, runs until ctrl+c is pressed.

.PARAMETER Stats
    Periodic socket statistics output.

.PARAMETER QueueCount
    Number of queues to spin.

.Parameter FuzzerCount
    Number of fuzzer threads per queue.

.Parameter SuccessThresholdPercent
    Minimum socket success rate, percent.

.PARAMETER CleanDatapath
    Avoid actions that invalidate the datapath.

.PARAMETER NoLogs
    Do not capture logs.

.PARAMETER XdpmpPollProvider
    Poll provider for XDPMP.

.PARAMETER EnableEbpf
    Enable eBPF in the XDP driver and spinxsk test cases.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [Int32]$Minutes = 0,

    [Parameter(Mandatory = $false)]
    [switch]$Stats = $false,

    [Parameter(Mandatory = $false)]
    [Int32]$QueueCount = 2,

    [Parameter(Mandatory = $false)]
    [Int32]$FuzzerCount = 0,

    [Parameter(Mandatory = $false)]
    [Int32]$SuccessThresholdPercent = -1,

    [Parameter(Mandatory = $false)]
    [switch]$CleanDatapath = $false,

    [Parameter(Mandatory = $false)]
    [switch]$NoLogs = $false,

    [Parameter(Mandatory = $false)]
    [switch]$BreakOnWatchdog = $false,

    [Parameter(Mandatory = $false)]
    [ValidateSet("NDIS", "FNDIS")]
    [string]$XdpmpPollProvider = "NDIS",

    [Parameter(Mandatory = $false)]
    [switch]$EnableEbpf = $false,

    [Parameter(Mandatory = $false)]
    [switch]$EbpfPreinstalled = $false
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

Write-Error "intentional error"
