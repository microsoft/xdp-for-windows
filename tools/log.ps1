<#

.SYNOPSIS
This helps start and stop ETW logging.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER Start
    Starts the logging.

.PARAMETER Stop
    Stops the logging.

.PARAMETER Convert
    Converts the ETL to text.

.PARAMETER Name
    The name of the tracing instance and output file.

.PARAMETER EtlPath
    Overrides the output ETL file.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [switch]$Start = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Stop = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Convert = $false,

    [Parameter(Mandatory = $false)]
    [string]$Profile = $null,

    [Parameter(Mandatory = $false)]
    [string]$Name = "xdp",

    [Parameter(Mandatory = $false)]
    [string]$EtlPath = $null,

    [Parameter(Mandatory = $false)]
    [ValidateSet("File", "Memory")]
    [string]$LogMode = "File"
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

if ($Profile -eq $null -and $Start) {
    Write-Error "-Start requires -Profile"
}

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$TracePdb = "$RootDir\artifacts\corenet-ci-main\vm-setup\tracepdb.exe"
$WprpFile = "$RootDir\tools\xdptrace.wprp"
$TmfPath = "$ArtifactsDir\tmfs"
$LogsDir = "$RootDir\artifacts\logs"
$LogPath = "$LogsDir\$Name.log"

if (!$EtlPath) {
    $EtlPath = "$LogsDir\$Name.etl"
}

if ($Start) {
    if (!(Test-Path $WprpFile)) {
        Write-Error "$WprpFile does not exist!"
    }

    $LogArg = ""
    if ($LogMode -eq "File") {
        $LogArg = "-filemode"
    }

    Write-Verbose "wpr.exe -start $($WprpFile)!$($Profile) -instancename $Name $LogArg"
    cmd /c "wpr.exe -start `"$($WprpFile)!$($Profile)`" -instancename $Name $LogArg 2>&1"
    if ($LastExitCode -ne 0) {
        Write-Host "##vso[task.setvariable variable=NeedsReboot]true"
        Write-Error "wpr.exe failed: $LastExitCode"
    }
}

if ($Stop) {
    New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

    Write-Verbose "wpr.exe -stop $EtlPath -instancename $Name"
    cmd /c "wpr.exe -stop $EtlPath -instancename $Name 2>&1"
    if ($LastExitCode -ne 0) {
        Write-Host "##vso[task.setvariable variable=NeedsReboot]true"
        Write-Error "wpr.exe failed: $LastExitCode"
    }

    # Enumerate log file sizes.
    Get-ChildItem $LogsDir | Format-Table | Out-String | Write-Verbose
}

if ($Convert) {
    if (!(Test-Path $EtlPath)) {
        Write-Error "$EtlPath does not exist!"
    }

    & $TracePdb -f "$ArtifactsDir\*.pdb" -p $TmfPath
    Invoke-Expression "netsh trace convert $($EtlPath) output=$($LogPath) tmfpath=$TmfPath overwrite=yes report=no"
}
