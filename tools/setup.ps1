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

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fndis", "xdp", "xdpmp", "xdpfnmp", "xdpfnlwf")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fndis", "xdp", "xdpmp", "xdpfnmp", "xdpfnlwf")]
    [string]$Uninstall = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("NDIS", "FNDIS")]
    [string]$XdpmpPollProvider = "NDIS"
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$LogsDir = "$RootDir\artifacts\logs"
$DevCon = "C:\devcon.exe"
$DswDevice = "C:\dswdevice.exe"
$LiveKD = "C:\livekd64.exe"
$KD = "C:\kd.exe"

# File paths.
$CodeSignCertPath = "$ArtifactsDir\CoreNetSignRoot.cer"
$XdpSys = "$ArtifactsDir\xdp\xdp.sys"
$XdpInf = "$ArtifactsDir\xdp\xdp.inf"
$XdpCat = "$ArtifactsDir\xdp\xdp.cat"
$FndisSys = "$ArtifactsDir\fndis\fndis.sys"
$XdpMpSys = "$ArtifactsDir\xdpmp\xdpmp.sys"
$XdpMpInf = "$ArtifactsDir\xdpmp\xdpmp.inf"
$XdpMpComponentId = "ms_xdpmp"
$XdpMpDeviceId = "xdpmp0"
$XdpMpServiceName = "XDPMP"
$XdpFnMpSys = "$ArtifactsDir\xdpfnmp\xdpfnmp.sys"
$XdpFnMpInf = "$ArtifactsDir\xdpfnmp\xdpfnmp.inf"
$XdpFnMpComponentId = "ms_xdpfnmp"
$XdpFnMpDeviceId0 = "xdpfnmp0"
$XdpFnMpDeviceId1 = "xdpfnmp1"
$XdpFnMpServiceName = "XDPFNMP"
$XdpFnLwfSys = "$ArtifactsDir\xdpfnlwf\xdpfnlwf.sys"
$XdpFnLwfInf = "$ArtifactsDir\xdpfnlwf\xdpfnlwf.inf"
$XdpFnLwfComponentId = "ms_xdpfnlwf"

# Helper to reboot the machine
function Uninstall-Failure {
    Write-Host "Capturing live kernel dump"

    New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null
    Write-Verbose "$LiveKD -o $LogsDir\xdp.dmp -k $KD -ml -accepteula"
    & $LiveKD -o $LogsDir\xdp.dmp -k $KD -ml -accepteula

    Write-Host "##vso[task.setvariable variable=NeedsReboot]true"
    Write-Error "Preparing to reboot machine!"
}

# Helper to start (with retry) a service.
function Start-Service-With-Retry($Name) {
    Write-Verbose "Start-Service $Name"
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

# Helper to rename (with retry) a network adapter. On WS2022, renames sometimes
# fail with ERROR_TRANSACTION_NOT_ACTIVE.
function Rename-NetAdapter-With-Retry($IfDesc, $Name) {
    Write-Verbose "Rename-NetAdapter $IfDesc $Name"
    $RenameSuccess = $false
    for ($i=0; $i -lt 10; $i++) {
        try {
            Rename-NetAdapter -InterfaceDescription $IfDesc $Name
            $RenameSuccess = $true
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if ($RenameSuccess -eq $false) {
        Write-Error "Failed to rename $Name"
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
            Write-Verbose "$Name failed to stop"
        }
    } catch {
        Write-Verbose "Exception while waiting for $Name to stop"
    }

    # Delete the service.
    if (Get-Service $Name -ErrorAction Ignore) {
        try { sc.exe delete $Name > $null }
        catch { Write-Verbose "'sc.exe delete $Name' threw exception!" }

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
            Write-Verbose "Failed to clean up $Name!"
            Uninstall-Failure
        }
    }
}

# Helper to wait for an adapter to start.
function Wait-For-Adapters($IfDesc, $Count=1, $WaitForUp=$true) {
    Write-Verbose "Waiting for $Count `"$IfDesc`" adapter(s) to start"
    $StartSuccess = $false
    for ($i = 0; $i -lt 100; $i++) {
        $Result = 0
        $Filter = { $_.InterfaceDescription -like "$IfDesc*" -and (!$WaitForUp -or $_.Status -eq "Up") }
        try { $Result = ((Get-NetAdapter | where $Filter) | Measure-Object).Count } catch {}
        if ($Result -eq $Count) {
            $StartSuccess = $true
            break;
        }
        Start-Sleep -Milliseconds 100
    }
    if ($StartSuccess -eq $false) {
        Get-NetAdapter | Format-Table | Out-String | Write-Verbose
        Write-Error "Failed to start $Count `"$IfDesc`" adapters(s) [$Result/$Count]"
    } else {
        Write-Verbose "Started $Count `"$IfDesc`" adapter(s)"
    }
}

# Helper to uninstall a driver from its inf file.
function Uninstall-Driver($Inf) {
    # Expected pnputil enum output is:
    #   Published Name: oem##.inf
    #   Original Name:  xdp.inf
    #   ...
    $DriverList = pnputil.exe /enum-drivers
    $StagedDriver = ""
    foreach ($line in $DriverList) {
        if ($line -match "Published Name") {
            $StagedDriver = $($line -split ":")[1]
        }

        if ($line -match "Original Name") {
            $infName = $($line -split ":")[1]
            if ($infName -match $Inf) {
                break
            }

            $StagedDriver = ""
        }
    }

    if ($StagedDriver -eq "") {
        Write-Verbose "Couldn't find $Inf in driver list."
        return
    }

    cmd.exe /c "pnputil.exe /delete-driver $StagedDriver 2>&1" | Write-Verbose
    if (!$?) {
        Write-Verbose "pnputil.exe /delete-driver $Inf ($StagedDriver) exit code: $LastExitCode"
    }
}

# Installs the xdp driver.
function Install-Xdp {
    if (!(Test-Path $XdpSys)) {
        Write-Error "$XdpSys does not exist!"
    }

    Write-Verbose "netcfg.exe -v -l $XdpInf -c s -i ms_xdp"
    netcfg.exe -v -l $XdpInf -c s -i ms_xdp | Write-Verbose
    if ($LastExitCode) {
        Write-Error "netcfg.exe exit code: $LastExitCode"
    }

    Start-Service-With-Retry xdp

    Write-Verbose "xdp.sys install complete!"
}

# Uninstalls the xdp driver.
function Uninstall-Xdp {
    Write-Verbose "netcfg.exe -u ms_xdp"
    cmd.exe /c "netcfg.exe -u ms_xdp 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "netcfg.exe failed: $LastExitCode"
    }

    Uninstall-Driver "xdp.inf"

    Cleanup-Service xdp

    Write-Verbose "xdp.sys uninstall complete!"
}

# Installs the fndis driver.
function Install-FakeNdis {
    if (!(Test-Path $FndisSys)) {
        Write-Error "$FndisSys does not exist!"
    }

    Write-Verbose "sc.exe create fndis type= kernel start= demand binpath= $FndisSys"
    sc.exe create fndis type= kernel start= demand binpath= $FndisSys | Write-Verbose
    if ($LastExitCode) {
        Write-Error "sc.exe exit code: $LastExitCode"
    }

    Start-Service-With-Retry fndis

    Write-Verbose "fndis.sys install complete!"
}

# Uninstalls the fndis driver.
function Uninstall-FakeNdis {
    Write-Verbose "Stop-Service fndis"
    try { Stop-Service fndis -NoWait } catch { }

    Cleanup-Service fndis

    Write-Verbose "fndis.sys uninstall complete!"
}

# Installs the xdpmp driver.
function Install-XdpMp {
    if (!(Test-Path $XdpMpSys)) {
        Write-Error "$XdpMpSys does not exist!"
    }

    Write-Verbose "pnputil.exe /install /add-driver $XdpMpInf"
    pnputil.exe /install /add-driver $XdpMpInf | Write-Verbose
    if ($LastExitCode) {
        Write-Error "pnputil.exe exit code: $LastExitCode"
    }

    Write-Verbose "dswdevice.exe -i $XdpMpDeviceId $XdpMpComponentId"
    & $DswDevice -i $XdpMpDeviceId $XdpMpComponentId | Write-Verbose
    if ($LastExitCode) {
        Write-Error "dswdevice.exe exit code: $LastExitCode"
    }

    # Do not wait for the adapter to fully come up: the default is NDIS polling,
    # which is not available prior to WS2022.
    Wait-For-Adapters -IfDesc $XdpMpServiceName -WaitForUp $false

    Write-Verbose "Renaming adapter"
    Rename-NetAdapter-With-Retry $XdpMpServiceName $XdpMpServiceName

    Write-Verbose "Get-NetAdapter $XdpMpServiceName"
    Get-NetAdapter $XdpMpServiceName | Format-Table | Out-String | Write-Verbose
    $AdapterIndex = (Get-NetAdapter $XdpMpServiceName).ifIndex

    Write-Verbose "Setting up the adapter"

    Write-Verbose "Set-NetAdapterAdvancedProperty -Name $XdpMpServiceName -RegistryKeyword PollProvider -DisplayValue $XdpmpPollProvider"
    Set-NetAdapterAdvancedProperty -Name $XdpMpServiceName -RegistryKeyword PollProvider -DisplayValue $XdpmpPollProvider

    if ($XdpmpPollProvider -eq "NDIS") {
        #Write-Verbose "Set-NetAdapterDataPathConfiguration -Name $XdpMpServiceName -Profile Passive"
        #Set-NetAdapterDataPathConfiguration -Name $XdpMpServiceName -Profile Passive
        Write-Verbose "Skipping NDIS polling configuration"
    }

    Wait-For-Adapters -IfDesc $XdpMpServiceName

    netsh.exe int ipv4 set int $AdapterIndex dadtransmits=0 | Write-Verbose
    netsh.exe int ipv4 add address $AdapterIndex address=192.168.100.1/24 | Write-Verbose
    netsh.exe int ipv4 add neighbor $AdapterIndex address=192.168.100.2 neighbor=22-22-22-22-00-02 | Write-Verbose

    netsh.exe int ipv6 set int $AdapterIndex dadtransmits=0 | Write-Verbose
    netsh.exe int ipv6 add address $AdapterIndex address=fc00::100:1/112 | Write-Verbose
    netsh.exe int ipv6 add neighbor $AdapterIndex address=fc00::100:2 neighbor=22-22-22-22-00-02 | Write-Verbose

    Write-Verbose "Adding firewall rules"
    netsh.exe advfirewall firewall add rule name="Allow$($XdpMpServiceName)v4" dir=in action=allow protocol=any remoteip=192.168.100.0/24 | Write-Verbose
    netsh.exe advfirewall firewall add rule name="Allow$($XdpMpServiceName)v6" dir=in action=allow protocol=any remoteip=fc00::100:0/112 | Write-Verbose

    Write-Verbose "xdpmp.sys install complete!"
}

# Uninstalls the xdpmp driver.
function Uninstall-XdpMp {
    netsh.exe advfirewall firewall del rule name="Allow$($XdpMpServiceName)v4" | Out-Null
    netsh.exe advfirewall firewall del rule name="Allow$($XdpMpServiceName)v6" | Out-Null

    cmd.exe /c "$DswDevice -u $XdpMpDeviceId 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Deleting $XdpMpDeviceId device failed: $LastExitCode"
    }

    cmd.exe /c "$DevCon remove @SWD\$XdpMpDeviceId\$XdpMpDeviceId 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Removing $XdpMpDeviceId device failed: $LastExitCode"
    }

    Cleanup-Service $XdpMpServiceName

    Uninstall-Driver "xdpmp.inf"

    Write-Verbose "xdpmp.sys uninstall complete!"
}

# Installs the xdpfnmp driver.
function Install-XdpFnMp {
    if (!(Test-Path $XdpFnMpSys)) {
        Write-Error "$XdpFnMpSys does not exist!"
    }

    Write-Verbose "pnputil.exe /install /add-driver $XdpFnMpInf"
    pnputil.exe /install /add-driver $XdpFnMpInf | Write-Verbose
    if ($LastExitCode) {
        Write-Error "pnputil.exe exit code: $LastExitCode"
    }

    Write-Verbose "dswdevice.exe -i $XdpFnMpDeviceId0 $XdpFnMpComponentId"
    & $DswDevice -i $XdpFnMpDeviceId0 $XdpFnMpComponentId | Write-Verbose
    if ($LastExitCode) {
        Write-Error "dswdevice.exe exit code: $LastExitCode"
    }
    Write-Verbose "dswdevice.exe -i $XdpFnMpDeviceId1 $XdpFnMpComponentId"
    & $DswDevice -i $XdpFnMpDeviceId1 $XdpFnMpComponentId | Write-Verbose
    if ($LastExitCode) {
        Write-Error "dswdevice.exe exit code: $LastExitCode"
    }

    Wait-For-Adapters -IfDesc $XdpFnMpServiceName -Count 2

    Write-Verbose "Renaming adapters"
    Rename-NetAdapter-With-Retry XDPFNMP XDPFNMP
    Rename-NetAdapter-With-Retry "XDPFNMP #2" XDPFNMP1Q

    Write-Verbose "Get-NetAdapter XDPFNMP"
    Get-NetAdapter XDPFNMP | Format-Table | Out-String | Write-Verbose
    Write-Verbose "Get-NetAdapter XDPFNMP1Q"
    Get-NetAdapter XDPFNMP1Q | Format-Table | Out-String | Write-Verbose

    Write-Verbose "Set-NetAdapterRss -Name XDPFNMP1Q -NumberOfReceiveQueues 1"
    Set-NetAdapterRss -Name XDPFNMP1Q -NumberOfReceiveQueues 1

    Write-Verbose "Configure xdpfnmp ipv4"
    netsh int ipv4 set int interface=xdpfnmp dadtransmits=0 | Write-Verbose
    netsh int ipv4 add address name=xdpfnmp address=192.168.200.1/24 | Write-Verbose
    netsh int ipv4 add neighbor xdpfnmp address=192.168.200.2 neighbor=22-22-22-22-00-02 | Write-Verbose

    Write-Verbose "Configure xdpfnmp ipv6"
    netsh int ipv6 set int interface=xdpfnmp dadtransmits=0 | Write-Verbose
    netsh int ipv6 add address interface=xdpfnmp address=fc00::200:1/112 | Write-Verbose
    netsh int ipv6 add neighbor xdpfnmp address=fc00::200:2 neighbor=22-22-22-22-00-02 | Write-Verbose

    Write-Verbose "Configure xdpfnmp1q ipv4"
    netsh int ipv4 set int interface=xdpfnmp1q dadtransmits=0 | Write-Verbose
    netsh int ipv4 add address name=xdpfnmp1q address=192.168.201.1/24 | Write-Verbose
    netsh int ipv4 add neighbor xdpfnmp1q address=192.168.201.2 neighbor=22-22-22-22-00-02 | Write-Verbose

    Write-Verbose "Configure xdpfnmp1q ipv6"
    netsh int ipv6 set int interface=xdpfnmp1q dadtransmits=0 | Write-Verbose
    netsh int ipv6 add address interface=xdpfnmp1q address=fc00::201:1/112 | Write-Verbose
    netsh int ipv6 add neighbor xdpfnmp1q address=fc00::201:2 neighbor=22-22-22-22-00-02 | Write-Verbose

    Write-Verbose "xdpfnmp.sys install complete!"
}

# Uninstalls the xdpfnmp driver.
function Uninstall-XdpFnMp {
    netsh int ipv4 delete address dpfnmp 192.168.200.1 | Out-Null
    netsh int ipv4 delete neighbors xdpfnmp | Out-Null
    netsh int ipv6 delete address xdpfnmp fc00::200:1 | Out-Null
    netsh int ipv6 delete neighbors xdpfnmp | Out-Null

    netsh int ipv4 delete address xdpfnmp1q 192.168.201.1 | Out-Null
    netsh int ipv4 delete neighbors xdpfnmp1q | Out-Null
    netsh int ipv6 delete address xdpfnmp1q fc00::201:1 | Out-Null
    netsh int ipv6 delete neighbors xdpfnmp1q | Out-Null

    cmd.exe /c "$DswDevice -u $XdpFnMpDeviceId1 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Deleting $XdpFnMpDeviceId1 device failed: $LastExitCode"
    }

    cmd.exe /c "$DevCon remove @SWD\$XdpFnMpDeviceId1\$XdpFnMpDeviceId1 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Removing $XdpFnMpDeviceId1 device failed: $LastExitCode"
    }

    cmd.exe /c "$DswDevice -u $XdpFnMpDeviceId0 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Deleting $XdpFnMpDeviceId0 device failed: $LastExitCode"
    }

    cmd.exe /c "$DevCon remove @SWD\$XdpFnMpDeviceId0\$XdpFnMpDeviceId0 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Removing $XdpFnMpDeviceId0 device failed: $LastExitCode"
    }

    Cleanup-Service $XdpFnMpServiceName

    Uninstall-Driver "xdpfnmp.inf"

    Write-Verbose "xdpfnmp.sys uninstall complete!"
}

# Installs the xdpfnlwf driver.
function Install-XdpFnLwf {
    if (!(Test-Path $XdpFnLwfSys)) {
        Write-Error "$XdpFnLwfSys does not exist!"
    }

    Write-Verbose "netcfg.exe -v -l $XdpFnLwfInf -c s -i $XdpFnLwfComponentId"
    netcfg.exe -v -l $XdpFnLwfInf -c s -i $XdpFnLwfComponentId | Write-Verbose
    if ($LastExitCode) {
        Write-Error "netcfg.exe exit code: $LastExitCode"
    }

    Start-Service-With-Retry xdpfnlwf

    Write-Verbose "xdpfnlwf.sys install complete!"
}

# Uninstalls the xdpfnlwf driver.
function Uninstall-XdpFnLwf {
    Write-Verbose "netcfg.exe -u $XdpFnLwfComponentId"
    cmd.exe /c "netcfg.exe -u $XdpFnLwfComponentId 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "netcfg.exe failed: $LastExitCode"
    }

    Uninstall-Driver "xdpfnlwf.inf"

    Cleanup-Service xdpfnlwf

    Write-Verbose "xdpfnlwf.sys uninstall complete!"
}

if ($Install -eq "fndis") {
    Install-FakeNdis
}
if ($Install -eq "xdp") {
    Install-Xdp
}
if ($Install -eq "xdpmp") {
    Install-XdpMp
}
if ($Install -eq "xdpfnmp") {
    Install-XdpFnMp
}
if ($Install -eq "xdpfnlwf") {
    Install-XdpFnLwf
}

if ($Uninstall -eq "fndis") {
    Uninstall-FakeNdis
}
if ($Uninstall -eq "xdp") {
    Uninstall-Xdp
}
if ($Uninstall -eq "xdpmp") {
    Uninstall-XdpMp
}
if ($Uninstall -eq "xdpfnmp") {
    Uninstall-XdpFnMp
}
if ($Uninstall -eq "xdpfnlwf") {
    Uninstall-XdpFnLwf
}
