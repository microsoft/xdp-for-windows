<#

.SYNOPSIS
This checks for the presence of any XDP drivers currently loaded.

#>

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

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

# Check for any XDP drivers.
if (Check-Driver 'fndis.sys') { Write-Error "fndis.sys loaded!" }
if (Check-Driver 'xdpfnmp.sys') { Write-Error "xdpfnmp.sys loaded!" }
if (Check-Driver 'xdpmp.sys') { Write-Error "xdpmp.sys loaded!" }
if (Check-Driver 'xdp.sys') { Write-Error "xdp.sys loaded!" }

# Yay! No XDP drivers found.
Write-Host "No loaded XDP drivers found!"

# Log driver verifier status
verifier.exe /query
