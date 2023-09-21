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

#Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'
#$ProgressPreference = 'SilentlyContinue'

# Set up the connection to the peer over remote powershell.
Write-Output "Connecting to $PeerName..."
$Session = New-PSSession -ComputerName $PeerName -ConfigurationName $RemotePSConfiguration
if ($null -eq $Session) {
    Write-Error "Failed to create remote session"
}

# Find all the local and remote IP and MAC addresses.
$RemoteAddress = [System.Net.Dns]::GetHostAddresses($Session.ComputerName)[0].IPAddressToString
Write-Output "Successfully connected to peer: $RemoteAddress"

$Route = Find-NetRoute -RemoteIPAddress $RemoteAddress -ErrorAction SilentlyContinue
if ($null -eq $Route) {
    Write-Error "Failed to find route to $RemoteAddress"
}
Write-Output "Route to peer:`n$Route"
$LocalAddress = $Route[0].IPAddress
Write-Output "Local address: $LocalAddress"

$LocalInterface = (Get-NetIPAddress -IPAddress $LocalAddress -ErrorAction SilentlyContinue).InterfaceIndex
$LocalMacAddress = (Get-NetAdapter -InterfaceIndex $LocalInterface).MacAddress
Write-Output "Local MAC address: $LocalMacAddress (on interface $LocalInterface)"

$out = Invoke-Command -Session $Session -ScriptBlock {
    param ($LocalAddress)
    $LocalInterface = (Get-NetIPAddress -IPAddress $LocalAddress -ErrorAction SilentlyContinue).InterfaceIndex
    return $LocalInterface, (Get-NetAdapter -InterfaceIndex $LocalInterface).MacAddress
} -ArgumentList $RemoteAddress
$RemoteInterface = $out[0]
$RemoteMacAddress = $out[1]
Write-Output "Remote MAC address: $RemoteMacAddress (on interface $RemoteInterface)"

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
tools\prepare-machine.ps1 -ForTest -NoReboot -Verbose
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
