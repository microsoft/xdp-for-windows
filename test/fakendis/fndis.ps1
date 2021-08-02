#
# Setup script for fndis.
#

param (
    [Parameter(Mandatory = $false)]
    [switch]$Install = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Uninstall = $false,

    [Parameter(Mandatory = $false)]
    [string]$SourceDirectory  = "."
)

if ($Uninstall) {
    Stop-Service fndis
    sc.exe delete fndis
    Remove-Item C:\Windows\System32\drivers\fndis.sys
}

if ($Install) {
    Copy-Item $SourceDirectory\fndis.sys C:\Windows\System32\drivers\fndis.sys
    sc.exe create fndis type= kernel start= boot binPath= C:\Windows\System32\drivers\fndis.sys
    Start-Service fndis
}
