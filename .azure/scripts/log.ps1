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
    Stops the logging and converts the ETL.

.PARAMETER Name
    The name of the tracing instance and output file.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x86", "x64", "arm", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [switch]$Start = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Stop = $false,

    [Parameter(Mandatory = $false)]
    [string]$Name = "xdp"
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Important paths.
$RootDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ArtifactsDir = Join-Path $RootDir "artifacts" "bin" "$($Arch)_$($Config)"
$ToolsDir = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x86"
$TracePdb = Join-Path $ToolsDir "tracepdb.exe"
$WprpFile = Join-Path $RootDir "test" "functional" "XdpFunctional.wprp"
$Profile = "XdpFunctional.Verbose"
$TmfPath = Join-Path $ArtifactsDir "tmfs"
$EtlPath = Join-Path $RootDir "artifacts" "logs" "$Name.etl"
$LogPath = Join-Path $RootDir "artifacts" "logs" "$Name.log"

function Start-Logging {
    Write-Host "+++++++ Starting logs +++++++"

    if (!(Test-Path $WprpFile)) {
        Write-Error "$WprpFile does not exist!"
    }

    try {
        Write-Debug "wpr.exe -start $($WprpFile)!$($Profile) -filemode -instancename $Name"
        wpr.exe -start "$($WprpFile)!$($Profile)" -filemode -instancename $Name 2>&1
    } catch {
        Write-Host $_
        Get-Error
        $_ | Format-List *
        throw
    }
}

function Stop-Logging {
    Write-Host "------- Stopping logs -------"
    try {
        wpr.exe -stop $EtlPath -instancename $Name 2>&1
        & $TracePdb -f (Join-Path $ArtifactsDir "*.pdb") -p $TmfPath
        Invoke-Expression "netsh trace convert $($EtlPath) output=$($LogPath) tmfpath=$TmfPath overwrite=yes report=no"
    } catch {
        Write-Host $_
        Get-Error
        $_ | Format-List *
        throw
    }
}

if ($Start) {
    Start-Logging
}

if ($Stop) {
    Stop-Logging
}
