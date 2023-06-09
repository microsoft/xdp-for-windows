#
# Wrapper script for XDPMP dynamic packet rate simulation.
#

param (
    [Parameter(Mandatory=$false)]
    [string]$AdapterName = "XDPMP",

    [Parameter(Mandatory=$false)]
    [UInt32]$RxFramesPerInterval = 0,

    [Parameter(Mandatory=$false)]
    [UInt32]$TxFramesPerInterval = 0,

    [Parameter(Mandatory=$false)]
    [switch]$Unlimited = $false
)

$Adapter = Get-NetAdapter -Name $AdapterName
$IfDesc = $Adapter.InterfaceDescription

if ($Unlimited) {
    $RxFramesPerInterval = "0xFFFFFFFF"
    $TxFramesPerInterval = "0xFFFFFFFF"
}

#
# If the adapter was freshly restarted, this configuration can race with various
# WMI registrations. Retry a few times as a workaround.
#
$Retries = 10
do {
    try {
        $Config = Get-CimInstance -Namespace root\wmi -Class XdpMpRateSim -Filter "InstanceName = '$IfDesc'"
        if ($Config -eq $null) {
            throw "WMI object not found."
        }

        if ($RxFramesPerInterval -gt 0) {
            $Config.RxFramesPerInterval = $RxFramesPerInterval
        }

        if ($TxFramesPerInterval -gt 0) {
            $Config.TxFramesPerInterval = $TxFramesPerInterval
        }

        Set-CimInstance $Config | Out-Null

        break
    } catch {
        Write-Warning "$($PSItem.Exception.Message)`n$($PSItem.ScriptStackTrace)"

        if ($Retries-- -eq 0) {
            Write-Error "Failed to configure rate simulation."
            break
        }

        Write-Warning "Retrying rate simulation configuration."
        Sleep -Milliseconds 500
    }
} while ($true)
