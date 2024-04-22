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
$Username = (Get-ItemProperty 'HKLM:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon').DefaultUserName
$Password = (Get-ItemProperty 'HKLM:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon').DefaultPassword | ConvertTo-SecureString -AsPlainText -Force
$Creds = New-Object System.Management.Automation.PSCredential ($Username, $Password)
$Session = New-PSSession -ComputerName $PeerName -Credential $Creds -ConfigurationName $RemotePSConfiguration
if ($null -eq $Session) {
    Write-Error "Failed to create remote session"
}

$RootDir = $pwd
. $RootDir\tools\common.ps1
$ArtifactBin = Get-ArtifactBinPath -Config $Config -Arch $Arch

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
$LocalVfAdapter = Get-NetAdapter | Where-Object {$_.MacAddress -eq $LocalMacAddress -and $_.ifIndex -ne $LocalInterface}
Write-Output "Local interface: $LocalInterface, $LocalMacAddress"

$LowestInterface = $LocalInterface
if ($LocalVfAdapter) {
    $script:LowestInterface = $LocalVfAdapter.ifIndex
}

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

Write-Output "`n====================SET UP====================`n"

# Copy the artifacts to the peer.
Write-Output "Copying files to peer..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($RemoteDir)
    Remove-Item -Force -Recurse $RemoteDir -ErrorAction Ignore
} -ArgumentList $RemoteDir
Copy-Item -ToSession $Session .\artifacts -Destination $RemoteDir\artifacts -Recurse
Copy-Item -ToSession $Session .\tools -Destination $RemoteDir\tools -Recurse

# Prepare the machines for the testing.
Write-Output "Preparing local machine for testing..."
tools\prepare-machine.ps1 -ForTest -NoReboot
Write-Output "Preparing remote machine for testing..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($RemoteDir)
    & $RemoteDir\tools\prepare-machine.ps1 -ForTest -NoReboot
} -ArgumentList $RemoteDir

# Check for any previously drivers.
Write-Output "Checking local machine state..."
tools\check-drivers.ps1 -Config $Config -Arch $Arch
Write-Output "Checking remote machine state..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir)
    & $RemoteDir\tools\check-drivers.ps1 -Config $Config -Arch $Arch
} -ArgumentList $Config, $Arch, $RemoteDir

try {
# Install eBPF on the machines.
Write-Output "Installing eBPF locally..."
tools\setup.ps1 -Install ebpf -Config $Config -Arch $Arch
Write-Output "Installing eBPF on peer..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir)
    & $RemoteDir\tools\setup.ps1 -Install ebpf -Config $Config -Arch $Arch
} -ArgumentList $Config, $Arch, $RemoteDir

# Install XDP on the machines.
# Set GenericDelayDetachTimeoutSec regkey to zero. This ensures XDP
# instantly detaches from the LWF data path whenever no programs are plumbed.
#
Write-Output "Installing XDP locally..."
tools\setup.ps1 -Install xdp -Config $Config -Arch $Arch -EnableEbpf
Write-Verbose "Setting GenericDelayDetachTimeoutSec = 0"
reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v GenericDelayDetachTimeoutSec /d 0 /t REG_DWORD /f | Write-Verbose
Write-Output "Installing XDP on peer..."
Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir)
    & $RemoteDir\tools\setup.ps1 -Install xdp -Config $Config -Arch $Arch -EnableEbpf
    Write-Verbose "Setting GenericDelayDetachTimeoutSec = 0"
    reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v GenericDelayDetachTimeoutSec /d 0 /t REG_DWORD /f | Write-Verbose
} -ArgumentList $Config, $Arch, $RemoteDir

# Allow wsario.exe through the remote firewall
Write-Output "Allowing wsario.exe through firewall..."
$WsaRio = Get-CoreNetCiArtifactPath -Name "WsaRio"
Write-Verbose "Adding firewall rules"
& netsh.exe advfirewall firewall add rule name="AllowWsaRio" program=$WsaRio dir=in action=allow protocol=any remoteip=$RemoteAddress | Write-Verbose

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

Write-Output "Configuring 1 RSS queue"
# Hyper-V NIC cannot configure NumberOfReceiveQueues, so configure MaxProcessors instead
Get-NetAdapter -ifIndex $LocalInterface | Set-NetAdapterRss -MaxProcessors 1
if ($LocalVfAdapter) {
    $LocalVfAdapter | Set-NetAdapterRss -NumberOfReceiveQueues 1
}
Write-Verbose "Waiting for IP address DAD to complete..."
Start-Sleep -Seconds 5

# Run xskbench latency mode and forward traffic on the peer.
Write-Output "Starting L2FWD on peer (forwarding on UDP 9999)..."
$Job = Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir, $LocalInterface)
    $RxFilter = "$RemoteDir\artifacts\bin\$($Arch)_$($Config)\rxfilter.exe"
    $RxFilterJob = & $RxFilter -IfIndex $LocalInterface -QueueId * -MatchType UdpDstPort -UdpDstPort 9999 -Action L2Fwd &
    Write-Output "Forwarding for 60 seconds"
    Start-Sleep -Seconds 60
    Stop-Job $RxFilterJob
    Receive-Job -Job $RxFilterJob -ErrorAction 'Continue'
} -ArgumentList $Config, $Arch, $RemoteDir, $RemoteInterface -AsJob

$TxBytes = & $PktCmd udp $LocalMacAddress $RemoteMacAddress $LocalAddress $RemoteAddress 9999 9999 8
Write-Debug "TX Payload:`n$TxBytes"

for ($i = 0; $i -lt 5; $i++) {
    Write-Output "Run $($i+1): Running xskbench locally (sending to and receiving on UDP 9999)..."
    $XskBench = "artifacts\bin\$($Arch)_$($Config)\xskbench.exe"
    & $XskBench lat -i $LowestInterface -d 10 -t -group 1 -ca 0x1 -q -id 0 -tx_pattern $TxBytes -b 8 -s
    Start-Sleep -Seconds 1
}

Write-Output "Waiting for remote RxFilter..."
Wait-Job -Job $Job | Out-Null
Receive-Job -Job $Job -ErrorAction 'Continue'

# Run xskbench.
Write-Output "Generating TX on the peer (sending to UDP 9999)..."
$Job = Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir, $RemoteAddress, $LocalInterface, $LocalAddress)
    . $RemoteDir\tools\common.ps1
    $WsaRio = Get-CoreNetCiArtifactPath -Name "WsaRio"
    & $WsaRio Winsock Send -Bind $LocalAddress -Target "$RemoteAddress`:9999" -IoCount -1 -MaxDuration 180 -ThreadCount 32 -Group 1 -CPU 0 -OptFlags 0x1 -IoSize 60000 -Uso 1000 -QueueDepth 1
} -ArgumentList $Config, $Arch, $RemoteDir, $LocalAddress, $RemoteInterface, $RemoteAddress -AsJob

for ($i = 0; $i -lt 5; $i++) {
    Start-Sleep -Seconds 1
    Write-Output "Run $($i+1): Running xskbench locally (receiving from UDP 9999) on 1 queue..."
    $XskBench = "artifacts\bin\$($Arch)_$($Config)\xskbench.exe"
    & $XskBench rx -i $LowestInterface -d 10 -p 9999 -t -group 1 -ca 0x1 -q -id 0 -b 8 -s
}

Write-Output "Configuring 8 RSS queues"
# Hyper-V NIC cannot configure NumberOfReceiveQueues, so configure MaxProcessors instead
Get-NetAdapter -ifIndex $LocalInterface | Set-NetAdapterRss -MaxProcessors 8
if ($LocalVfAdapter) {
    $LocalVfAdapter | Set-NetAdapterRss -NumberOfReceiveQueues 8
}

for ($i = 0; $i -lt 5; $i++) {
    Write-Output "Starting local detailed logs..."
    try { wpr.exe -cancel -instancename "xskfunc$i" 2>&1 | Out-Null } catch { }
    tools\log.ps1 -Start -Name "xskfunc$i" -Profile XdpFunctional.Verbose -Config $Config -Arch $Arch

    Start-Sleep -Seconds 1
    Write-Output "Run $($i+1): Running xskbench locally (receiving from UDP 9999) on 8 queues..."
    $XskBench = "artifacts\bin\$($Arch)_$($Config)\xskbench.exe"
    & $XskBench rx -i $LowestInterface -d 10 -p 9999 -t -group 1 -ca 0x1 -q -id 0 -b 8 -s -q -id 1 -b 8 -s -q -id 2 -b 8 -s -q -id 3 -b 8 -s -q -id 4 -b 8 -s -q -id 5 -b 8 -s -q -id 6 -b 8 -s -q -id 7 -b 8 -s

    Write-Output "Stopping local logs..."
    tools\log.ps1 -Stop -Name "xskfunc$i" -Config $Config -Arch $Arch -EtlPath artifacts\logs\"xskfunc$i"-local.etl -ErrorAction 'Continue' | Out-Null
}

Write-Output "Waiting for remote xskbench..."
Wait-Job -Job $Job | Out-Null
Receive-Job -Job $Job -ErrorAction 'Continue'

# Run wsario.
Write-Output "Starting wsario on the peer (sending to UDP 9999)..."
$Job = Invoke-Command -Session $Session -ScriptBlock {
    param ($Config, $Arch, $RemoteDir, $RemoteAddress, $LocalInterface, $LocalAddress)
    . $RemoteDir\tools\common.ps1
    $WsaRio = Get-CoreNetCiArtifactPath -Name "WsaRio"
    & $WsaRio Winsock Send -Bind $LocalAddress -Target "$RemoteAddress`:9999" -IoCount -1 -MaxDuration 180 -ThreadCount 32 -Group 1 -CPU 0 -OptFlags 0x1 -IoSize 60000 -Uso 1000 -QueueDepth 1
} -ArgumentList $Config, $Arch, $RemoteDir, $LocalAddress, $RemoteInterface, $RemoteAddress -AsJob

foreach ($XdpMode in "None", "BuiltIn", "eBPF") {
    switch ($XdpMode) {
        BuiltIn
        {
            Write-Output "Attaching BuiltIn program"
            $RxFilterJob = & $ArtifactBin\rxfilter.exe -IfIndex $LocalInterface -QueueId * -MatchType All -Action Pass &
        }
        eBPF
        {
            Write-Output "Attaching eBPF program"
            $ProgId = (& netsh.exe ebpf add program $ArtifactBin\bpf\pass.sys interface=$LocalInterface)[0].substring("Loaded with ID ".length)
        }
    }

    for ($i = 0; $i -lt 5; $i++) {
        Start-Sleep -Seconds 1
        Write-Output "Run $($i+1): Running wsario locally (receiving from UDP 9999)..."
        $WsaRio = Get-CoreNetCiArtifactPath -Name "WsaRio"
        & $WsaRio Winsock Receive -Bind "$LocalAddress`:9999" -IoCount -1 -MaxDuration 10 -ThreadCount 8 -Group 1 -CPU 0 -CPUOffset 2 -OptFlags 0x9 -QueueDepth 1 -IoSize 65535 -Uro 65535
    }

    switch ($XdpMode) {
        BuiltIn
        {
            Write-Output "Stopping BuiltIn program"
            Stop-Job $RxFilterJob
            Receive-Job -Job $RxFilterJob -ErrorAction 'Continue'
        }
        eBPF
        {
            Write-Output "Stopping eBPF program"
            & netsh.exe ebpf delete program $ProgId
        }
    }
}

Write-Output "Waiting for remote wsario..."
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
    Write-Output "Removing WsaRio firewall rule"
    & netsh.exe advfirewall firewall del rule name="AllowWsaRio" | Out-Null
    # Clean up XDP driver state.
    Write-Output "Removing XDP locally..."
    tools\setup.ps1 -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    Write-Output "Removing XDP on peer..."
    Invoke-Command -Session $Session -ScriptBlock {
        param ($Config, $Arch, $RemoteDir)
        & $RemoteDir\tools\setup.ps1 -Uninstall xdp -Config $Config -Arch $Arch -ErrorAction 'Continue'
    } -ArgumentList $Config, $Arch, $RemoteDir
    # Clean up eBPF
    Write-Output "Removing eBPF locally..."
    tools\setup.ps1 -Uninstall ebpf -Config $Config -Arch $Arch -ErrorAction 'Continue'
    Write-Output "Removing eBPF on peer..."
    Invoke-Command -Session $Session -ScriptBlock {
        param ($Config, $Arch, $RemoteDir)
        & $RemoteDir\tools\setup.ps1 -Uninstall ebpf -Config $Config -Arch $Arch -ErrorAction 'Continue'
    } -ArgumentList $Config, $Arch, $RemoteDir
}
