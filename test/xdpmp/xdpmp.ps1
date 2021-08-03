#
# Setup script for xdpmp.
#

param (
    [Parameter(Mandatory = $false)]
    [switch]$Install = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Uninstall = $false,

    [Parameter(Mandatory = $false)]
    [switch]$DriverPreinstalled = $false,

    [Parameter(Mandatory = $false)]
    [string]$SourceDirectory  = ".",

    [Parameter(Mandatory = $false)]
    [switch]$Fndis = $false
)

if ($Fndis) {
    $DeviceId = "xdpmpf0"
    $ComponentId = "ms_xdpmpf"
    $Name = "XDPMPF"
} else {
    $DeviceId = "xdpmp0"
    $ComponentId = "ms_xdpmp"
    $Name = "XDPMP"
}

if ($Uninstall) {
    netsh advfirewall firewall del rule name="Allow$($Name)v4"
    netsh advfirewall firewall del rule name="Allow$($Name)v6"

    & "$SourceDirectory\dswdevice.exe" -u $DeviceId

    # Wait for the driver to stop.
    for ($i = 0; $i -lt 100; $i++) {
        if (-not (Get-Service $Name -ErrorAction Ignore) -or
            (Get-Service $Name).Status -eq "Stopped") {
            break;
        }
        Start-Sleep -Milliseconds 100
    }

    if (-not $DriverPreinstalled) {
        sc.exe delete $Name
        $XdpMpDriverFile = (Get-WindowsDriver -Online | where {$_.OriginalFileName -like "*xdpmp.inf" }).Driver
        pnputil.exe /uninstall /delete-driver $XdpMpDriverFile 2> $null
    }
}

if ($Install) {
    if (-not $DriverPreinstalled) {
        # The driver might have already been previously installed via another
        # method. This might have been done if pnputil is not available on the
        # machine, which is the case for OneCore-based images.
        pnputil.exe /install /add-driver "$SourceDirectory\xdpmp.inf"
    }

    & "$SourceDirectory\dswdevice.exe" -i $DeviceId $ComponentId

    # Wait for the NIC to start.
    for ($i = 0; $i -lt 100; $i++) {
        if (Get-NetAdapter -InterfaceDescription $Name -ErrorAction Ignore) {
            break;
        }
        Start-Sleep -Milliseconds 100
    }

    Rename-NetAdapter -InterfaceDescription $Name $Name
    $AdapterIndex = (Get-NetAdapter $Name).ifIndex

    netsh int ipv4 set int $AdapterIndex dadtransmits=0
    netsh int ipv4 add address $AdapterIndex address=192.168.100.1/24
    netsh int ipv4 add neighbor $AdapterIndex address=192.168.100.2 neighbor=22-22-22-22-00-02

    netsh int ipv6 set int $AdapterIndex dadtransmits=0
    netsh int ipv6 add address $AdapterIndex address=fc00::100:1/112
    netsh int ipv6 add neighbor $AdapterIndex address=fc00::100:2 neighbor=22-22-22-22-00-02

    netsh advfirewall firewall add rule name="Allow$($Name)v4" dir=in action=allow protocol=any remoteip=192.168.100.0/24
    netsh advfirewall firewall add rule name="Allow$($Name)v6" dir=in action=allow protocol=any remoteip=fc00::100:0/112

    Set-NetAdapterDataPathConfiguration -Name $Name -Profile Passive
}
