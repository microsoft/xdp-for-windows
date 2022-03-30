<#

.SYNOPSIS
This checks for the presence of any XDP drivers currently loaded.

#>

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent

# Cache driverquery output.
$AllDrivers = driverquery /v

# Checks for the presence of a loaded driver.
function Check-Driver($Driver) {
    $Found = $false
    try {
        $Found = ($AllDrivers | Select-String $Driver).Matches[0].Success
    } catch { }
    $Found
}

# Checks for the presence of a loaded driver and attempts to uninstall it.
function Check-And-Remove-Driver($Driver, $Component) {
    if (Check-Driver $Driver) {
        & $RootDir\tools\setup.ps1 -Uninstall $Component

        # Update cached driverquery output.
        $AllDrivers = driverquery /v
    }

    Check-Driver $Driver
}

# Check for any XDP drivers.
if (Check-And-Remove-Driver "fndis.sys" "fndis") { Write-Error "fndis.sys loaded!" }
if (Check-And-Remove-Driver "xdpfnmp.sys" "xdpfnmp") { Write-Error "xdpfnmp.sys loaded!" }
if (Check-And-Remove-Driver "xdpfnlwf.sys" "xdpfnlwf") { Write-Error "xdpfnmp.sys loaded!" }
if (Check-And-Remove-Driver "xdpmp.sys" "xdpmp") { Write-Error "xdpmp.sys loaded!" }
if (Check-And-Remove-Driver "xdp.sys" "xdp") { Write-Error "xdp.sys loaded!" }

# Yay! No XDP drivers found.
Write-Host "No loaded XDP drivers found!"

# Log driver verifier status
verifier.exe /query
