param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [string]$PeerName = "netperf-peer",

    [Parameter(Mandatory = $false)]
    [string]$RemotePSConfiguration = "PowerShell.7"
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Set up the connection to the peer over remote powershell.
Write-Output "Connecting to $PeerName..."
$Session = New-PSSession -ComputerName $PeerName -ConfigurationName $RemotePSConfiguration
if ($null -eq $Session) {
    Write-Error "Failed to create remote session"
    exit
}
function Convert-HostToNetworkOrder {
    param ($Address)
    $Bytes = $Address.GetAddressBytes()
    [Array]::Reverse($Bytes) | Out-Null
    return [System.BitConverter]::ToUInt32($Bytes, 0)
}

class IpData {
    [Int64]$PrefixLength;
    [System.Net.IPAddress]$IPv4Address;
    IpData([Int64]$PrefixLength, [System.Net.IPAddress]$Address) {
        $this.PrefixLength = $PrefixLength;
        $this.IPv4Address = $Address;
    }
}

function Get-Ipv4Addresses {
    $LocalIps = [System.Collections.Generic.List[IpData]]::new();
    $Nics = [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces();
    foreach ($Nic in $Nics) {
        if ($Nic.OperationalStatus -ne [System.Net.NetworkInformation.OperationalStatus]::Up) {
            continue;
        }
        $UniAddresses = $Nic.GetIPProperties().UnicastAddresses;
        if ($null -eq $UniAddresses) {
            continue;
        }
        foreach ($UniAddress in $UniAddresses) {
            $Addr = $UniAddress.Address;
            if ($Addr.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
                continue;
            }
            $LocalIps.Add([IpData]::new($UniAddress.PrefixLength, $Addr))
        }
    }
    return $LocalIps;
}

function Get-LocalAddress {
    param ($RemoteName)
    $PossibleRemoteIPs = [System.Net.Dns]::GetHostAddresses($RemoteName) | Select-Object -Property IPAddressToString
    $PossibleLocalIPs = Get-Ipv4Addresses
    $MatchedIPs = @()
    $PossibleLocalIPs | ForEach-Object {

        [IPAddress]$LocalIpAddr = $_.IPv4Address

        $ToMaskLocalAddress = Convert-HostToNetworkOrder -Address $LocalIpAddr

        $Mask = (1uL -shl $_.PrefixLength) - 1
        $Mask = $Mask -shl (32 - $_.PrefixLength)
        $LocalSubnet = $ToMaskLocalAddress -band $Mask

        $PossibleRemoteIPs | ForEach-Object {
            [ipaddress]$RemoteIpAddr = $_.IPAddressToString
            $ToMaskRemoteAddress = Convert-HostToNetworkOrder($RemoteIpAddr)
            $RemoteMasked = $ToMaskRemoteAddress -band $Mask

            if ($RemoteMasked -eq $LocalSubnet) {
                $MatchedIPs += $LocalIpAddr.IPAddressToString
            }
        }
    }

    if ($MatchedIPs.Length -ne 1) {
        Write-Error "Failed to parse local address matching remote"
    }

    return $MatchedIPs[0]
}

# Find all the local and remote IP and MAC addresses.
$RemoteAddress = [System.Net.Dns]::GetHostAddresses($Session.ComputerName)[0].IPAddressToString
Write-Output "Successfully connected to peer: $RemoteAddress"

$LocalAddress = Get-LocalAddress $Session.ComputerName # (Find-NetRoute -RemoteIPAddress $RemoteAddress).IPAddress
Write-Output "Local address: $LocalAddress"

$LocalMacAddress = (Get-NetAdapter | Where-Object { (Get-NetIPAddress -InterfaceIndex $_.ifIndex).IPAddress -contains $LocalAddress }).MacAddress
Write-Output "Local MAC address: $LocalMacAddress"

$RemoteMacAddress = Invoke-Command -Session $Session -ScriptBlock {
    param ($LocalAddress)
    return (Get-NetAdapter | Where-Object { (Get-NetIPAddress -InterfaceIndex $_.ifIndex).IPAddress -contains $LocalAddress }).MacAddress
} -ArgumentList $RemoteAddress
Write-Output "Remote MAC address: $RemoteMacAddress"

# Copy the artifacts to the peer.
Write-Output "Copying files to peer..."
Invoke-Command -Session $Session -ScriptBlock {
    Remove-Item -Force -Recurse "C:\_work" -ErrorAction Ignore
}
Copy-Item -ToSession $Session .\artifacts -Destination C:\_work\artifacts -Recurse
Copy-Item -ToSession $Session .\tools -Destination C:\_work\tools -Recurse

# Check the drivers.
tools\check-drivers.ps1 -Config $Config -Arch $Arch -Verbose
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch)
    C:\_work\tools\check-drivers.ps1 -Config $Config -Arch $Arch -Verbose
} -ArgumentList $Config, $Arch

# Prepare the machines for the testing.
Write-Output "Preparing machines for testing..."
.\tools\prepare-machine.ps1 -ForTest -NoReboot -Verbose
Invoke-Command -Session $Session -ScriptBlock {
    C:\_work\tools\prepare-machine.ps1 -ForTest -NoReboot -Verbose
}

try {
# Install XDP on the machines.
Write-Output "Installing XDP locally..."
tools\setup.ps1 -Install xdp -Config $Config -Arch $Arch
Write-Output "Installing XDP on peer..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch)
    C:\_work\tools\setup.ps1 -Install xdp -Config $Config -Arch $Arch
} -ArgumentList $Config, $Arch

# Run xskbench on the server.
# TODO

} finally {
    Write-Output "Removing XDP locally..."
    tools\setup.ps1 -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    Write-Output "Removing XDP on peer..."
    Invoke-Command -Session $Session -ScriptBlock {
        param ($Config, $Arch)
        C:\_work\tools\setup.ps1 -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    } -ArgumentList $Config, $Arch
}
