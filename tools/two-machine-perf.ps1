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

# Find all the local and remote IP and MAC addresses.
$RemoteAddress = [System.Net.Dns]::GetHostAddresses($Session.ComputerName)[0].IPAddressToString
Write-Output "Successfully connected to peer: $RemoteAddress"

Write-Output "`nDEBUG STATE:"
Find-NetRoute -RemoteIPAddress $RemoteAddress
Get-NetIPAddress
Get-NetAdapter
Write-Output "============================================"

$LocalAddress = (Find-NetRoute -RemoteIPAddress $RemoteAddress -ErrorAction SilentlyContinue).IPAddress
Write-Output "Local address: $LocalAddress"

$LocalInterface = (Get-NetIPAddress -IPAddress $LocalAddress -ErrorAction SilentlyContinue).InterfaceIndex
$LocalMacAddress = (Get-NetAdapter -InterfaceIndex $LocalInterface).MacAddress
Write-Output "Local MAC address: $LocalMacAddress"

$RemoteMacAddress = Invoke-Command -Session $Session -ScriptBlock {
    param ($LocalAddress)
    $LocalInterface = (Get-NetIPAddress -IPAddress $LocalAddress -ErrorAction SilentlyContinue).InterfaceIndex
    return (Get-NetAdapter -InterfaceIndex $LocalInterface).MacAddress
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
