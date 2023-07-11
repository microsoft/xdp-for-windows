<#

.SYNOPSIS
This script installs or uninstalls various XDP components.

.PARAMETER Install
    Specifies an XDP component to install.

.PARAMETER Uninstall
    Attempts to uninstall all XDP components.

#>

param (

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "xdp")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "xdp")]
    [string]$Uninstall = ""
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

$InstallDir = $PSScriptRoot

# Global paths.
$XdpInf = "$InstallDir\xdp.inf"
$XdpPcwMan = "$InstallDir\xdppcw.man"

# Helper wait for a service to stop and then delete it. Callers are responsible
# making sure the service is already stopped or stopping.
function Cleanup-Service($Name) {
    # Wait for the service to stop.
    $StopSuccess = $false
    try {
        for ($i = 0; $i -lt 100; $i++) {
            if (-not (Get-Service $Name -ErrorAction Ignore) -or
                (Get-Service $Name).Status -eq "Stopped") {
                $StopSuccess = $true
                break;
            }
            Start-Sleep -Milliseconds 100
        }
        if (!$StopSuccess) {
            Write-Verbose "$Name failed to stop"
        }
    } catch {
        Write-Verbose "Exception while waiting for $Name to stop"
    }

    # Delete the service.
    if (Get-Service $Name -ErrorAction Ignore) {
        try { sc.exe delete $Name > $null }
        catch { Write-Verbose "'sc.exe delete $Name' threw exception!" }

        # Wait for the service to be deleted.
        $DeleteSuccess = $false
        for ($i = 0; $i -lt 10; $i++) {
            if (-not (Get-Service $Name -ErrorAction Ignore)) {
                $DeleteSuccess = $true
                break;
            }
            Start-Sleep -Milliseconds 10
        }
        if (!$DeleteSuccess) {
            Write-Error "Failed to clean up $Name!"
        }
    }
}

# Helper to uninstall a driver from its inf file.
function Uninstall-Driver($Inf) {
    # Expected pnputil enum output is:
    #   Published Name: oem##.inf
    #   Original Name:  xdp.inf
    #   ...
    $DriverList = pnputil.exe /enum-drivers
    $StagedDriver = ""
    foreach ($line in $DriverList) {
        if ($line -match "Published Name") {
            $StagedDriver = $($line -split ":")[1]
        }

        if ($line -match "Original Name") {
            $infName = $($line -split ":")[1]
            if ($infName -match $Inf) {
                break
            }

            $StagedDriver = ""
        }
    }

    if ($StagedDriver -eq "") {
        Write-Verbose "Couldn't find $Inf in driver list."
        return
    }

    cmd.exe /c "pnputil.exe /delete-driver $StagedDriver 2>&1" | Write-Verbose
    if (!$?) {
        Write-Verbose "pnputil.exe /delete-driver $Inf ($StagedDriver) exit code: $LastExitCode"
    }
}

# Installs the xdp driver.
if ($Install -eq "xdp") {
    if (!(Test-Path "$XdpInf")) {
        Write-Error "$XdpInf does not exist!"
    }

    Write-Verbose "netcfg.exe -v -l '$XdpInf' -c s -i ms_xdp"
    netcfg.exe -v -l "$XdpInf" -c s -i ms_xdp | Write-Verbose
    if ($LastExitCode) {
        Write-Error "netcfg.exe exit code: $LastExitCode"
    }

    Write-Verbose "lodctr.exe /m:$XdpPcwMan $env:WINDIR\system32\drivers\"
    lodctr.exe /m:$XdpPcwMan $env:WINDIR\system32\drivers\ | Write-Verbose
    if ($LastExitCode) {
        Write-Error "lodctr.exe exit code: $LastExitCode"
    }

    Write-Verbose "xdp.sys install complete!"
}

# Uninstalls the xdp driver.
if ($Uninstall -eq "xdp") {

    Write-Verbose "unlodctr.exe /m:$XdpPcwMan"
    unlodctr.exe /m:$XdpPcwMan | Write-Verbose
    if ($LastExitCode) {
        Write-Error "unlodctr.exe exit code: $LastExitCode"
    }

    Write-Verbose "netcfg.exe -u ms_xdp"
    cmd.exe /c "netcfg.exe -u ms_xdp 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "netcfg.exe failed: $LastExitCode"
    }

    Uninstall-Driver "xdp.inf"

    Cleanup-Service xdp

    Remove-Item $env:WINDIR\system32\xdpapi.dll -Force
    Remove-Item $env:WINDIR\system32\drivers\xdp.sys -Force

    Write-Verbose "xdp.sys uninstall complete!"
}
