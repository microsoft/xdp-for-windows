#
# Setup script for xdp.
#

param (
    [Parameter(Mandatory = $false)]
    [switch]$Install = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Uninstall = $false,

    [Parameter(Mandatory = $false)]
    [switch]$DriverPreinstalled = $false,

    [Parameter(Mandatory = $false)]
    [string]$SourceDirectory  = "."
)

$XdpLwfGuid = "{c0be1ebc-74b8-4ba9-8c1e-ecd227e2be3b}";

if ($Uninstall) {
    Stop-Service xdp
    if (-not $DriverPreinstalled) {
        netcfg.exe -u ms_xdp
        $XdpDriverFile = (Get-WindowsDriver -Online | where {$_.OriginalFileName -like "*xdp.inf" }).Driver
        pnputil.exe /uninstall /delete-driver $XdpDriverFile 2> $null
    } else {
        & "$SourceDirectory\netsetuptool.exe" -uninstall -type filter -id $XdpLwfGuid
        sc.exe delete xdp
    }
    Remove-Item C:\Windows\System32\xdpapi.dll
}

if ($Install) {
    Copy-Item $SourceDirectory\xdpapi.dll C:\Windows\System32\
    if (-not $DriverPreinstalled) {
        netcfg.exe -l "$SourceDirectory\xdp.inf" -c s -i ms_xdp
    } else {
        sc.exe create xdp type= kernel binpath= "$SourceDirectory\xdp.sys" start= auto group= NDIS displayName= "xdp"
        & "$SourceDirectory\netsetuptool.exe" -install -type filter -id $XdpLwfGuid -friendlyname "XDP" -bottom ethernet -filterClass custom -comp ms_xdp -optional
        sc.exe start xdp
    }
}
