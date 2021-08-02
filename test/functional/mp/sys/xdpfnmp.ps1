#
# Setup script for xdpfnmp.
#

param (
    [Parameter(Mandatory = $false)]
    [switch]$Install = $false,

    [Parameter(Mandatory = $false)]
    [switch]$Uninstall = $false,

    [Parameter(Mandatory = $false)]
    [switch]$DriverPreinstalled = $false,

    [Parameter(Mandatory=$false)]
    [string]$SourceDirectory  = "."
)

if ($Uninstall) {
    & "$SourceDirectory\dswdevice.exe" -u xdpfnmp1
    & "$SourceDirectory\dswdevice.exe" -u xdpfnmp0

    # Wait for the driver to stop.
    for ($i = 0; $i -lt 100; $i++) {
        if (-not (Get-Service xdpfnmp -ErrorAction Ignore) -or
            (Get-Service xdpfnmp).Status -eq "Stopped") {
            break;
        }
        Start-Sleep -Milliseconds 100
    }

    if (-not $DriverPreinstalled) {
        sc.exe delete xdpfnmp
        $XdpFnMpDriverFile = (Get-WindowsDriver -Online | where {$_.OriginalFileName -like "*xdpfnmp.inf" }).Driver
        pnputil.exe /uninstall /delete-driver $XdpFnMpDriverFile 2> $null
    }
}

if ($Install) {
    if (-not $DriverPreinstalled) {
        # The driver might have already been previously installed via another
        # method. This might have been done if pnputil is not available on the
        # machine, which is the case for OneCore-based images.
        pnputil.exe /install /add-driver "$SourceDirectory\xdpfnmp.inf"
    }

    & "$SourceDirectory\dswdevice.exe" -i xdpfnmp0 ms_xdpfnmp
    & "$SourceDirectory\dswdevice.exe" -i xdpfnmp1 ms_xdpfnmp

    # Wait for the NIC to start.
    for ($i = 0; $i -lt 100; $i++) {
        if ((Get-NetAdapter -InterfaceDescription XDPFNMP -ErrorAction Ignore).Count -eq 2) {
            break;
        }
        Start-Sleep -Milliseconds 100
    }

    Rename-NetAdapter -InterfaceDescription XDPFNMP XDPFNMP

    Rename-NetAdapter -InterfaceDescription "XDPFNMP #2" XDPFNMP1Q
    Set-NetAdapterRss -Name XDPFNMP1Q -NumberOfReceiveQueues 1

    netsh int ipv4 set int interface=xdpfnmp dadtransmits=0
    netsh int ipv4 add address name=xdpfnmp address=192.168.200.1/24
    netsh int ipv4 add neighbor xdpfnmp address=192.168.200.2 neighbor=22-22-22-22-00-02

    netsh int ipv6 set int interface=xdpfnmp dadtransmits=0
    netsh int ipv6 add address interface=xdpfnmp address=fc00::200:1/112
    netsh int ipv6 add neighbor xdpfnmp address=fc00::200:2 neighbor=22-22-22-22-00-02

    netsh int ipv4 set int interface=xdpfnmp1q dadtransmits=0
    netsh int ipv4 add address name=xdpfnmp1q address=192.168.201.1/24
    netsh int ipv4 add neighbor xdpfnmp1q address=192.168.201.2 neighbor=22-22-22-22-00-02

    netsh int ipv6 set int interface=xdpfnmp1q dadtransmits=0
    netsh int ipv6 add address interface=xdpfnmp1q address=fc00::201:1/112
    netsh int ipv6 add neighbor xdpfnmp1q address=fc00::201:2 neighbor=22-22-22-22-00-02
}
