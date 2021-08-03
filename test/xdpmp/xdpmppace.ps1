#
# Wrapper script for dynamic pacing.
#

param (
    [Parameter(Mandatory=$true)]
    [string]$AdapterName,

    [Parameter(Mandatory=$false)]
    [UInt32]$RxFramesPerInterval = 0,

    [Parameter(Mandatory=$false)]
    [UInt32]$TxFramesPerInterval = 0
)

$Adapter = Get-NetAdapter -Name $AdapterName
$IfDesc = $Adapter.InterfaceDescription

#
# If the adapter was freshly restarted, this configuration can race with various
# WMI registrations. Retry a few times as a workaround.
#
$Retries = 10
do {
    try {
        $PacingConfig = Get-WmiObject -Namespace root\wmi -Class XdpMpPacing -Filter "InstanceName = '$IfDesc'"
        if ($PacingConfig -eq $null) {
            throw "WMI object not found."
        }

        if ($RxFramesPerInterval -gt 0) {
            $PacingConfig.RxFramesPerInterval = $RxFramesPerInterval
        }

        if ($TxFramesPerInterval -gt 0) {
            $PacingConfig.TxFramesPerInterval = $TxFramesPerInterval
        }

        $PacingConfig.Put() | Out-Null

        break
    } catch {
        Write-Warning "$($PSItem.Exception.Message)`n$($PSItem.ScriptStackTrace)"

        if ($Retries-- -eq 0) {
            Write-Error "Failed to configure pacing."
            break
        }

        Write-Warning "Retrying pacing configuration."
        Sleep -Milliseconds 500
    }
} while ($true)
