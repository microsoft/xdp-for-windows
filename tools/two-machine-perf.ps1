param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64")]
    [string]$Arch = "x64"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Set up the connection to the peer over remote powershell.
Write-Output "Connecting to netperf-peer..."
$Session = New-PSSession -ComputerName "netperf-peer" -ConfigurationName PowerShell.7
if ($null -eq $Session) {
    Write-Error "Failed to create remote session"
    exit
}
$RemoteAddress = $Session.ComputerName
Write-Output "Successfully conencted to peer: $RemoteAddress"

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
    C:\_work\quic\tools\check-drivers.ps1 -Config $Config -Arch $Arch -Verbose
}
run: tools/prepare-machine.ps1 -ForPerfTest -NoReboot -Verbose

# Prepare the machines for the testing.
Write-Output "Preparing machines for testing..."
.\tools\prepare-machine.ps1 -ForPerfTest -NoReboot -Verbose
Invoke-Command -Session $Session -ScriptBlock {
    C:\_work\quic\tools\prepare-machine.ps1 -ForPerfTest -NoReboot -Verbose
}

# Run xskbench on the server.
Write-Output "Starting xskbench server..."
# TODO

