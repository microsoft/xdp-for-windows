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
    [string]$RemotePSConfiguration = "PowerShell.7",

    [Parameter(Mandatory = $false)]
    [string]$RemoteDir = "C:\_work"
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
$LocalAddress = $Route[0].IPAddress
Write-Output "Local address: $LocalAddress"

$LocalInterface = (Get-NetIPAddress -IPAddress $LocalAddress -ErrorAction SilentlyContinue).InterfaceIndex
$LocalMacAddress = (Get-NetAdapter -InterfaceIndex $LocalInterface).MacAddress
Write-Output "Local interface: $LocalInterface, $LocalMacAddress"

$out = Invoke-Command -Session $Session -ScriptBlock {
    param ($LocalAddress)
    $LocalInterface = (Get-NetIPAddress -IPAddress $LocalAddress -ErrorAction SilentlyContinue).InterfaceIndex
    return $LocalInterface, (Get-NetAdapter -InterfaceIndex $LocalInterface).MacAddress
} -ArgumentList $RemoteAddress
$RemoteInterface = $out[0]
$RemoteMacAddress = $out[1]
Write-Output "Remote interface: $RemoteInterface, $RemoteMacAddress"

# Generate payload to send to the peer.
$PktCmd = "artifacts\bin\$($Arch)_$($Config)\pktcmd.exe"
$TxBytes = & $PktCmd udp $LocalMacAddress $RemoteMacAddress $LocalAddress $RemoteAddress 9999 9999 1280
Write-Debug "TX Payload:`n$TxBytes"

Write-Output "`n====================SET UP====================`n"

# Copy the artifacts to the peer.
Write-Output "Copying files to peer..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($RemoteDir)
    Remove-Item -Force -Recurse $RemoteDir -ErrorAction Ignore
} -ArgumentList $RemoteDir
Copy-Item -ToSession $Session .\artifacts -Destination $RemoteDir\artifacts -Recurse
Copy-Item -ToSession $Session .\tools -Destination $RemoteDir\tools -Recurse

# Check for any previously drivers.
Write-Output "Checking local machine state..."
tools\check-drivers.ps1 -Config $Config -Arch $Arch
Write-Output "Checking remote machine state..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir)
    & $RemoteDir\tools\check-drivers.ps1 -Config $Config -Arch $Arch
} -ArgumentList $Config, $Arch, $RemoteDir

# Prepare the machines for the testing.
Write-Output "Preparing local machine for testing..."
tools\prepare-machine.ps1 -ForNetPerfTest -NoReboot
Write-Output "Preparing remote machine for testing..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($RemoteDir)
    & $RemoteDir\tools\prepare-machine.ps1 -ForNetPerfTest -NoReboot
} -ArgumentList $RemoteDir

try {
# Install XDP on the machines.
Write-Output "Installing XDP locally..."
tools\setup.ps1 -Install xdp -Config $Config -Arch $Arch
Write-Output "Installing XDP on peer..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir)
    & $RemoteDir\tools\setup.ps1 -Install xdp -Config $Config -Arch $Arch
} -ArgumentList $Config, $Arch, $RemoteDir

# Start logging.
Write-Output "Starting local logs..."
try { wpr.exe -cancel -instancename xskcpu 2>&1 | Out-Null } catch { }
tools\log.ps1 -Start -Name xskcpu -Profile CpuSample.Verbose -Config $Config -Arch $Arch
Write-Output "Starting remote logs..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir)
    try { wpr.exe -cancel -instancename xskcpu 2>&1 | Out-Null } catch { }
    & $RemoteDir\tools\log.ps1 -Start -Name xskcpu -Profile CpuSample.Verbose -Config $Config -Arch $Arch
} -ArgumentList $Config, $Arch, $RemoteDir

Write-Output "`n====================EXECUTION====================`n"

# Run xskbench.
Write-Output "Starting xskbench on the peer (listening on UDP 9999)..."
$Job = Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir, $LocalInterface)
    $XskBench = "$RemoteDir\artifacts\bin\$($Arch)_$($Config)\xskbench.exe"
    & $XskBench rx -i $LocalInterface -d 60 -p 9999 -t -ci 0 -q -id 0
} -ArgumentList $Config, $Arch, $RemoteDir, $RemoteInterface -AsJob

for ($i = 0; $i -lt 5; $i++) {
    Write-Output "Run $($i+1): Running xskbench locally (sending to UDP 9999)..."
    $XskBench = "artifacts\bin\$($Arch)_$($Config)\xskbench.exe"
    & $XskBench tx -i $LocalInterface -d 10 -t -ci 0 -q -id 0 -tx_pattern $TxBytes -b 8
    Start-Sleep -Seconds 1
}

Write-Output "Waiting for remote xskbench..."
Wait-Job -Job $Job | Out-Null
Receive-Job -Job $Job -ErrorAction 'Continue'

Write-Output "Test Complete!"

} finally {
    Write-Output "`n====================CLEAN UP====================`n"
    # Grab the logs.
    Write-Output "Stopping remote logs..."
    Invoke-Command -Session $Session -ScriptBlock {
        param ($Config, $Arch, $RemoteDir)
        & $RemoteDir\tools\log.ps1 -Stop -Name xskcpu -Config $Config -Arch $Arch -EtlPath $RemoteDir\artifacts\logs\xskbench-peer.etl -ErrorAction 'Continue' | Out-Null
    } -ArgumentList $Config, $Arch, $RemoteDir
    Write-Output "Stopping local logs..."
    tools\log.ps1 -Stop -Name xskcpu -Config $Config -Arch $Arch -EtlPath artifacts\logs\xskbench-local.etl -ErrorAction 'Continue' | Out-Null
    Write-Output "Copying remote logs..."
    Copy-Item -FromSession $Session $RemoteDir\artifacts\logs\xskbench-peer.etl -Destination artifacts\logs -ErrorAction 'Continue'
    # Clean up XDP driver state.
    Write-Output "Removing XDP locally..."
    tools\setup.ps1 -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    Write-Output "Removing XDP on peer..."
    Invoke-Command -Session $Session -ScriptBlock {
        param ($Config, $Arch, $RemoteDir)
        & $RemoteDir\tools\setup.ps1 -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    } -ArgumentList $Config, $Arch, $RemoteDir
}
