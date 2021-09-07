<#

.SYNOPSIS
This script installs or uninstalls various XDP components.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER Install
    Specifies an XDP component to install.

.PARAMETER Uninstall
    Attempts to uninstall all XDP components.

.PARAMETER FailureAction
    Action to take, if any, when uninstall fails.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x86", "x64", "arm", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "xdpmp", "xdpfnmp")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [switch]$Uninstall = $false,

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "reboot", "bugcheck")]
    [string]$FailureAction = "bugcheck"
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Important paths.
$RootDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ArtifactsDir = Join-Path $RootDir "artifacts" "bin" "$($Arch)_$($Config)"
$DswDevice = "C:\Program Files (x86)\Windows Kits\10\Tools\x86\dswdevice.exe"

# File paths.
$XdpSys = Join-Path $ArtifactsDir "xdp" "xdp.sys"
$XdpInf = Join-Path $ArtifactsDir "xdp" "xdp.inf"
$XdpCat = Join-Path $ArtifactsDir "xdp" "xdp.cat"
$FndisSys = Join-Path $ArtifactsDir "fndis" "fndis.sys"
$XdpMpSys = Join-Path $ArtifactsDir "xdpmp" "xdpmp.sys"
$XdpMpInf = Join-Path $ArtifactsDir "xdpmp" "xdpmp.inf"
$XdpMpComponentId = "ms_xdpmp"
$XdpMpDeviceId = "xdpmp0"
$XdpMpServiceName = "XDPMP"
$XdpFnMpSys = Join-Path $ArtifactsDir "xdpfnmp" "xdpfnmp.sys"
$XdpFnMpInf = Join-Path $ArtifactsDir "xdpfnmp" "xdpfnmp.inf"
$XdpFnMpComponentId = "ms_xdpfnmp"
$XdpFnMpDeviceId0 = "xdpfnmp0"
$XdpFnMpDeviceId1 = "xdpfnmp1"
$XdpFnMpServiceName = "XDPFNMP"

# Helper to reboot the machine
function Uninstall-Failure {
    if ($FailureAction -eq "reboot") {
        Write-Host "##vso[task.setvariable variable=NeedsReboot]true"
        Write-Error "Preparing to reboot machine!"
    } elseif ($FailureAction -eq "bugcheck") {
        Write-Host "Forcing a bugcheck!"
        Write-Debug "C:\notmyfault64.exe /bugcheck aabbccdd"
        Start-Sleep -Seconds 5
        C:\notmyfault64.exe /bugcheck aabbccdd
    }
}

# Helper to start (with retry) a service.
function Start-Service-With-Retry($Name) {
    Write-Debug "Start-Service $Name"
    $StartSuccess = $false
    for ($i=0; $i -lt 10; $i++) {
        try {
            Start-Sleep -Milliseconds 100
            Start-Service $Name
            $StartSuccess = $true
            break
        } catch { }
    }
    if ($StartSuccess -eq $false) {
        Write-Error "Failed to start $Name"
    }
}

# Helper wait for a service to stop and then delete it. Callers are responsible
# making sure the service is already stopped or stopping.
function Cleanup-Service($Name) {
    # Wait for the service to stop.
    $StopSuccess = $false
    try {
        for ($i = 0; $i -lt 100; $i++) {
            if (-not (Get-Service $Name -ErrorAction Ignore) -or
                (Get-Service $Name).Status -eq "Stopped") {
                $StopSuccess = $true
                break;
            }
            Start-Sleep -Milliseconds 100
        }
        if (!$StopSuccess) {
            Write-Debug "$Name failed to stop"
        }
    } catch {
        Write-Debug "Exception while waiting for $Name to stop"
    }

    # Delete the service.
    if (Get-Service $Name -ErrorAction Ignore) {
        try { sc.exe delete $Name > $null }
        catch { Write-Debug "'sc.exe delete $Name' threw exception!" }

        # Wait for the service to be deleted.
        $DeleteSuccess = $false
        for ($i = 0; $i -lt 10; $i++) {
            if (-not (Get-Service $Name -ErrorAction Ignore)) {
                $DeleteSuccess = $true
                break;
            }
            Start-Sleep -Milliseconds 10
        }
        if (!$DeleteSuccess) {
            Write-Debug "Failed to clean up $Name!"
            Uninstall-Failure
        }
    }
}

# Helper to wait for an adapter to start.
function Wait-For-Adapter($Name) {
    Write-Debug "Waiting for $Name to start"
    $StartSuccess = $false
    for ($i = 0; $i -lt 100; $i++) {
        if (Get-NetAdapter -InterfaceDescription $Name -ErrorAction Ignore) {
            $StartSuccess = $true
            break;
        }
        Start-Sleep -Milliseconds 100
    }
    if ($StartSuccess -eq $false) {
        Write-Error "Failed to start $Name"
    }
}

# Helper to uninstall a driver from its inf file.
function Uninstall-Driver($Inf) {
    try {
        $DriverFile = (Get-WindowsDriver -Online | where {$_.OriginalFileName -like "*$Inf" }).Driver
        pnputil.exe /uninstall /delete-driver $DriverFile > $null
        if ($LastExitCode) {
            Write-Error "pnputil.exe exit code: $LastExitCode"
        }
    } catch { Write-Debug "Uninstalling $Inf failed" }
}

# Installs the xdp driver.
function Install-Xdp {
    Write-Host "+++++++ Installing xdp.sys +++++++"

    # Verify all the files are present.
    if (!(Test-Path $XdpSys)) {
        Write-Error "$XdpSys does not exist!"
    }

    # Enable verifier on xdp.
    Write-Debug "verifier.exe /volatile /adddriver xdp.sys /flags 0x9BB"
    verifier.exe /volatile /adddriver xdp.sys /flags 0x9BB > $null
    if ($LastExitCode) {
        Write-Host "verifier.exe exit code: $LastExitCode"
    }

    # Install xdp via inf.
    Write-Debug "netcfg.exe -v -l $XdpInf -c s -i ms_xdp"
    netcfg.exe -v -l $XdpInf -c s -i ms_xdp
    if ($LastExitCode) {
        Write-Error "netcfg.exe exit code: $LastExitCode"
    }

    Write-Debug "xdp.sys install complete!"
}

# Uninstalls the xdp driver.
function Uninstall-Xdp {
    Write-Host "------- Uninstalling xdp.sys -------"

    # Uninstall xdp.
    try { netcfg.exe -u ms_xdp > $null } catch { Write-Debug "netcfg failure" }
    try { pnputil.exe /uninstall /delete-driver $XdpSys > $null } catch { Write-Debug "pnputil failure" }

    # Clean up verifier.
    try { verifier.exe /volatile /removedriver xdp.sys > $null } catch { Write-Debug "verifier (removal) failure" }
    try { verifier.exe /volatile /flags 0x0 > $null } catch { Write-Debug "verifier (clean up flags) failure" }

    Write-Debug "xdp.sys uninstall complete!"
}

# Installs the fndis driver.
function Install-FakeNdis {
    Write-Host "+++++++ Installing fndis.sys +++++++"

    # Verify all the files are present.
    if (!(Test-Path $FndisSys)) {
        Write-Error "$FndisSys does not exist!"
    }

    # Create a service for fndis.
    Write-Debug "sc.exe create fndis type= kernel start= demand binpath= $FndisSys"
    sc.exe create fndis type= kernel start= demand binpath= $FndisSys
    if ($LastExitCode) {
        Write-Error "sc.exe exit code: $LastExitCode"
    }

    # Start the service.
    Start-Service-With-Retry fndis

    Write-Debug "fndis.sys install complete!"
}

# Uninstalls the fndis driver.
function Uninstall-FakeNdis {
    Write-Host "------- Uninstalling fndis.sys -------"

    # Stop the service.
    Write-Debug "Stop-Service fndis"
    try { Stop-Service fndis } catch { }

    # Cleanup the service.
    Cleanup-Service fndis

    Write-Debug "fndis.sys uninstall complete!"
}

# Installs the xdpmp driver.
function Install-XdpMp {
    Write-Host "+++++++ Installing xdpmp.sys +++++++"

    # Verify all the files are present.
    if (!(Test-Path $XdpMpSys)) {
        Write-Error "$XdpMpSys does not exist!"
    }

    # Install the xdpmp driver via inf.
    Write-Debug "pnputil.exe /install /add-driver $XdpMpInf"
    pnputil.exe /install /add-driver $XdpMpInf
    if ($LastExitCode) {
        Write-Error "pnputil.exe exit code: $LastExitCode"
    }

    # Install the device.
    Write-Debug "dswdevice.exe -i $XdpMpDeviceId $XdpMpComponentId"
    & $DswDevice -i $XdpMpDeviceId $XdpMpComponentId
    if ($LastExitCode) {
        Write-Error "devcon.exe exit code: $LastExitCode"
    }

    # Wait for the NIC to start.
    Wait-For-Adapter $XdpMpServiceName

    # Set up the adapter.
    Write-Debug "Renaming adapter"
    Rename-NetAdapter -InterfaceDescription $XdpMpServiceName $XdpMpServiceName

    # Get the adapter.
    Write-Debug "Get-NetAdapter $XdpMpServiceName"
    Get-NetAdapter $XdpMpServiceName
    $AdapterIndex = (Get-NetAdapter $XdpMpServiceName).ifIndex

    # Set up the adatper's paramters.
    Write-Debug "Setting up the adapter"
    netsh.exe int ipv4 set int $AdapterIndex dadtransmits=0
    netsh.exe int ipv4 add address $AdapterIndex address=192.168.100.1/24
    netsh.exe int ipv4 add neighbor $AdapterIndex address=192.168.100.2 neighbor=22-22-22-22-00-02

    netsh.exe int ipv6 set int $AdapterIndex dadtransmits=0
    netsh.exe int ipv6 add address $AdapterIndex address=fc00::100:1/112
    netsh.exe int ipv6 add neighbor $AdapterIndex address=fc00::100:2 neighbor=22-22-22-22-00-02

    # Add some firewall rules.
    Write-Debug "Adding firewall rules"
    netsh.exe advfirewall firewall add rule name="Allow$($XdpMpServiceName)v4" dir=in action=allow protocol=any remoteip=192.168.100.0/24
    netsh.exe advfirewall firewall add rule name="Allow$($XdpMpServiceName)v6" dir=in action=allow protocol=any remoteip=fc00::100:0/112

    Write-Debug "Set-NetAdapterDataPathConfiguration -Name $XdpMpServiceName -Profile Passive"
    Set-NetAdapterDataPathConfiguration -Name $XdpMpServiceName -Profile Passive

    Write-Debug "xdpmp.sys install complete!"
}

# Uninstalls the xdpmp driver.
function Uninstall-XdpMp {
    Write-Host "------- Uninstalling xdpmp.sys -------"

    netsh.exe advfirewall firewall del rule name="Allow$($XdpMpServiceName)v4"
    netsh.exe advfirewall firewall del rule name="Allow$($XdpMpServiceName)v6"

    # Remove the device.
    try { & $DswDevice -u $XdpMpDeviceId } catch { Write-Debug "Deleting $XdpMpDeviceId device failed" }

    # Cleanup the service.
    Cleanup-Service $XdpMpServiceName

    # Uninstall xdpmp.
    Uninstall-Driver "xdpmp.inf"

    Write-Debug "xdpmp.sys uninstall complete!"
}

# Installs the xdpfnmp driver.
function Install-XdpFnMp {
    Write-Host "+++++++ Installing xdpfnmp.sys +++++++"

    # Verify all the files are present.
    if (!(Test-Path $XdpFnMpSys)) {
        Write-Error "$XdpFnMpSys does not exist!"
    }

    # Install the xdpmp driver via inf.
    Write-Debug "pnputil.exe /install /add-driver $XdpFnMpInf"
    pnputil.exe /install /add-driver $XdpFnMpInf
    if ($LastExitCode) {
        Write-Error "pnputil.exe exit code: $LastExitCode"
    }

    # Install the devices.
    Write-Debug "dswdevice.exe -i $XdpFnMpDeviceId0 $XdpFnMpComponentId"
    & $DswDevice -i $XdpFnMpDeviceId0 $XdpFnMpComponentId
    if ($LastExitCode) {
        Write-Error "devcon.exe exit code: $LastExitCode"
    }
    Write-Debug "dswdevice.exe -i $XdpFnMpDeviceId1 $XdpFnMpComponentId"
    & $DswDevice -i $XdpFnMpDeviceId1 $XdpFnMpComponentId
    if ($LastExitCode) {
        Write-Error "devcon.exe exit code: $LastExitCode"
    }

    # Wait for the NIC to start.
    Wait-For-Adapter $XdpFnMpServiceName

    # Set up the adapter.
    Write-Debug "Renaming adapters"
    Rename-NetAdapter -InterfaceDescription XDPFNMP XDPFNMP
    Rename-NetAdapter -InterfaceDescription "XDPFNMP #2" XDPFNMP1Q

    # Get the adapter.
    Write-Debug "Get-NetAdapter XDPFNMP"
    Get-NetAdapter XDPFNMP
    Write-Debug "Get-NetAdapter XDPFNMP1Q"
    Get-NetAdapter XDPFNMP1Q

    # Set up the adatper's paramters.
    Write-Debug "Set-NetAdapterRss -Name XDPFNMP1Q -NumberOfReceiveQueues 1"
    Set-NetAdapterRss -Name XDPFNMP1Q -NumberOfReceiveQueues 1

    Write-Debug "Configure xdpfnmp ipv4"
    netsh int ipv4 set int interface=xdpfnmp dadtransmits=0
    netsh int ipv4 add address name=xdpfnmp address=192.168.200.1/24
    netsh int ipv4 add neighbor xdpfnmp address=192.168.200.2 neighbor=22-22-22-22-00-02

    Write-Debug "Configure xdpfnmp ipv6"
    netsh int ipv6 set int interface=xdpfnmp dadtransmits=0
    netsh int ipv6 add address interface=xdpfnmp address=fc00::200:1/112
    netsh int ipv6 add neighbor xdpfnmp address=fc00::200:2 neighbor=22-22-22-22-00-02

    Write-Debug "Configure xdpfnmp1q ipv4"
    netsh int ipv4 set int interface=xdpfnmp1q dadtransmits=0
    netsh int ipv4 add address name=xdpfnmp1q address=192.168.201.1/24
    netsh int ipv4 add neighbor xdpfnmp1q address=192.168.201.2 neighbor=22-22-22-22-00-02

    Write-Debug "Configure xdpfnmp1q ipv6"
    netsh int ipv6 set int interface=xdpfnmp1q dadtransmits=0
    netsh int ipv6 add address interface=xdpfnmp1q address=fc00::201:1/112
    netsh int ipv6 add neighbor xdpfnmp1q address=fc00::201:2 neighbor=22-22-22-22-00-02

    Write-Debug "xdpfnmp.sys install complete!"
}

# Uninstalls the xdpfnmp driver.
function Uninstall-XdpFnMp {
    Write-Host "------- Uninstalling xdpfnmp.sys -------"

    netsh int ipv4 delete address dpfnmp 192.168.200.1 2>&1 $null
    netsh int ipv4 delete neighbors xdpfnmp 2>&1 $null
    netsh int ipv6 delete address xdpfnmp fc00::200:1 2>&1 $null
    netsh int ipv6 delete neighbors xdpfnmp 2>&1 $null

    netsh int ipv4 delete address xdpfnmp1q 192.168.201.1 2>&1 $null
    netsh int ipv4 delete neighbors xdpfnmp1q 2>&1 $null
    netsh int ipv6 delete address xdpfnmp1q fc00::201:1 2>&1 $null
    netsh int ipv6 delete neighbors xdpfnmp1q 2>&1 $null

    # Remove the device.
    try { & $DswDevice -u $XdpFnMpDeviceId1 } catch { Write-Debug "Deleting $XdpMpDeviceId1 device failed" }
    try { & $DswDevice -u $XdpFnMpDeviceId0 } catch { Write-Debug "Deleting $XdpMpDeviceId0 device failed" }

    # Cleanup the service.
    Cleanup-Service $XdpFnMpServiceName

    # Uninstall xdpfnmp.
    Uninstall-Driver "xdpfnmp.inf"

    Write-Debug "xdpfnmp.sys uninstall complete!"
}

if ($Install -ne "") {
    # Install the necessary components.
    if ($Install -eq "xdpmp") {
        try {
            Install-FakeNdis
            Install-Xdp
            Install-XdpMp
        } catch {
            Write-Host "================Exception Thrown================"
            Write-Host $_
            Uninstall-XdpMp
            Uninstall-Xdp
            Uninstall-FakeNdis
            throw
        }

    } elseif ($Install -eq "xdpfnmp") {
        try {
            Install-Xdp
            Install-XdpFnMp
        } catch {
            Write-Host "================Exception Thrown================"
            Write-Host $_
            Uninstall-XdpFnMp
            Uninstall-Xdp
            throw
        }
    }
    $LastExitCode = 0
}

if ($Uninstall) {
    # Just try to uninstall everything.
    Uninstall-XdpFnMp
    Uninstall-XdpMp
    Uninstall-Xdp
    Uninstall-FakeNdis
}
