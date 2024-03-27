<#

.SYNOPSIS
This helps start and stop ETW logging.

.PARAMETER Config
    When using an artifacts directory, specifies the build configuration to use.

.PARAMETER Arch
    When using an artifacts directory, specifies the CPU architecture to use.

.PARAMETER SymbolPath
    Specifies a directory containing symbol files.

.PARAMETER Start
    Starts logging.

.PARAMETER Profile
    Specifies the WPR profile to start logging.

.PARAMETER Stop
    Stops logging.

.PARAMETER Convert
    Converts the ETL to text. Requires a symbol path, either implicitly via the
    Config and Arch parameters or explicitly via SymbolPath.

.PARAMETER Name
    The name or wildcard pattern of the tracing instance and output file.

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
    [string]$SymbolPath = $null,

    [Parameter(Mandatory = $false)]
    [switch]$Start = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Stop = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Convert = $false,

    [Parameter(Mandatory = $false)]
    [string]$Profile = "XDP",

    [Parameter(Mandatory = $false)]
    [SupportsWildcards()]
    [string]$Name = "xdp",

    [Parameter(Mandatory = $false)]
    [string]$EtlPath = $null,

    [Parameter(Mandatory = $false)]
    [ValidateSet("File", "Memory")]
    [string]$LogMode = "File"
)

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$TracePdb = Get-CoreNetCiArtifactPath -Name "tracepdb.exe"
$WprpFile = "$RootDir\tools\xdptrace.wprp"
$TmfPath = "$ArtifactsDir\tmfs"
$LogsDir = "$RootDir\artifacts\logs"

& $RootDir/tools/prepare-machine.ps1 -ForLogging

if (!$EtlPath) {
    $EtlPath = "$LogsDir\$Name.etl"
}

try {
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
        if (!(Get-ChildItem $EtlPath)) {
            Write-Error "$EtlPath does not exist!"
        }

        if (!$SymbolPath) {
            $SymbolPath = $ArtifactsDir
        }

        & $TracePdb -f "$SymbolPath\*.pdb" -p $TmfPath

        $FnSymbolsDir = "$(Get-FnRuntimeDir)\symbols"
        if (Test-Path $FnSymbolsDir) {
            & $TracePdb -f "$FnSymbolsDir\*.pdb" -p $TmfPath
        }

        foreach ($Etl in Get-ChildItem $EtlPath) {
            Invoke-Expression "netsh trace convert $Etl tmfpath=$TmfPath overwrite=yes report=no"
        }
    }
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
