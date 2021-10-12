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
    [ValidateSet("x86", "x64", "arm", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fndis", "xdp", "xdpmp", "xdpfnmp")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fndis", "xdp", "xdpmp", "xdpfnmp")]
    [string]$Uninstall = "",

    [Parameter(Mandatory = $false)]
    [switch]$Verifier = $false
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$LogsDir = "$RootDir\artifacts\logs"
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

# Helper to reboot the machine
function Uninstall-Failure {
    Write-Host "Capturing live kernel dump"

    New-Item -ItemType Directory -Force -Path $LogsDir
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
function Wait-For-Adapters($IfDesc, $Count=1) {
    Write-Verbose "Waiting for $Count `"$IfDesc`" adapter(s) to start"
    $StartSuccess = $false
    for ($i = 0; $i -lt 100; $i++) {
        $Result = 0
        $Filter = { $_.InterfaceDescription -like "$IfDesc*" -and $_.Status -eq "Up" }
        try { $Result = ((Get-NetAdapter | where $Filter) | Measure-Object).Count } catch {}
        if ($Result -eq $Count) {
            $StartSuccess = $true
            break;
        }
        Start-Sleep -Milliseconds 100
    }
    if ($StartSuccess -eq $false) {
        Write-Error "Failed to start $Count `"$IfDesc`" adapters(s) [$Result/$Count]"
    } else {
        Write-Verbose "Started $Count `"$IfDesc`" adapter(s)"
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
    } catch { Write-Verbose "Uninstalling $Inf failed" }
}

# Installs the xdp driver.
function Install-Xdp {
    Write-Host "+++++++ Installing xdp.sys +++++++"

    if (!(Test-Path $XdpSys)) {
        Write-Error "$XdpSys does not exist!"
    }

    if ($Verifier) {
        Write-Verbose "verifier.exe /volatile /adddriver xdp.sys /flags 0x9BB"
        verifier.exe /volatile /adddriver xdp.sys /flags 0x9BB > $null
        if ($LastExitCode) {
            Write-Host "verifier.exe exit code: $LastExitCode"
        }
    }

    Write-Verbose "netcfg.exe -v -l $XdpInf -c s -i ms_xdp"
    netcfg.exe -v -l $XdpInf -c s -i ms_xdp
    if ($LastExitCode) {
        Write-Error "netcfg.exe exit code: $LastExitCode"
    }

    Write-Verbose "xdp.sys install complete!"
}

# Uninstalls the xdp driver.
function Uninstall-Xdp {
    Write-Host "------- Uninstalling xdp.sys -------"

    try { netcfg.exe -u ms_xdp > $null } catch { Write-Verbose "netcfg failure" }
    try { pnputil.exe /uninstall /delete-driver $XdpSys > $null } catch { Write-Verbose "pnputil failure" }

    if ($Verifier) {
        try { verifier.exe /volatile /removedriver xdp.sys > $null } catch { Write-Verbose "verifier (removal) failure" }
        try { verifier.exe /volatile /flags 0x0 > $null } catch { Write-Verbose "verifier (clean up flags) failure" }
    }

    Write-Verbose "xdp.sys uninstall complete!"
}

# Installs the fndis driver.
function Install-FakeNdis {
    Write-Host "+++++++ Installing fndis.sys +++++++"

    if (!(Test-Path $FndisSys)) {
        Write-Error "$FndisSys does not exist!"
    }

    if ($Verifier) {
        Write-Verbose "verifier.exe /volatile /adddriver fndis.sys /flags 0x9BB"
        verifier.exe /volatile /adddriver fndis.sys /flags 0x9BB > $null
        if ($LastExitCode) {
            Write-Host "verifier.exe exit code: $LastExitCode"
        }
    }

    Write-Verbose "sc.exe create fndis type= kernel start= demand binpath= $FndisSys"
    sc.exe create fndis type= kernel start= demand binpath= $FndisSys
    if ($LastExitCode) {
        Write-Error "sc.exe exit code: $LastExitCode"
    }

    Start-Service-With-Retry fndis

    Write-Verbose "fndis.sys install complete!"
}

# Uninstalls the fndis driver.
function Uninstall-FakeNdis {
    Write-Host "------- Uninstalling fndis.sys -------"

    Write-Verbose "Stop-Service fndis"
    try { Stop-Service fndis } catch { }

    Cleanup-Service fndis

    if ($Verifier) {
        try { verifier.exe /volatile /removedriver fndis.sys > $null } catch { Write-Verbose "verifier (removal) failure" }
    }

    Write-Verbose "fndis.sys uninstall complete!"
}

# Installs the xdpmp driver.
function Install-XdpMp {
    Write-Host "+++++++ Installing xdpmp.sys +++++++"

    if (!(Test-Path $XdpMpSys)) {
        Write-Error "$XdpMpSys does not exist!"
    }

    # On Server 2022 LTSC, the IO verifier flag causes a bugcheck due to a bug
    # in NT verifier and PNP verifier. Either don't enable verifier on XDPMP, or
    # disable IO verifier on all drivers when enabling verifier on XDPMP. To
    # ensure we get as close to maximum coverage as possible, randomly enable
    # verifier on XDPMP *or* do nothing and implicitly keep the IO verifier flag
    # enabled.
    if ($Verifier) {
        if ((Get-Random -Maximum 1) -eq 1) {
            Write-Verbose "verifier.exe /volatile /adddriver xdpmp.sys /flags 0x9AB"
            verifier.exe /volatile /adddriver xdpmp.sys /flags 0x9AB > $null
            if ($LastExitCode) {
                Write-Host "verifier.exe exit code: $LastExitCode"
            }
        } else {
            Write-Verbose "Not enabling verifier on xdpmp.sys"
        }
    }

    Write-Verbose "pnputil.exe /install /add-driver $XdpMpInf"
    pnputil.exe /install /add-driver $XdpMpInf
    if ($LastExitCode) {
        Write-Error "pnputil.exe exit code: $LastExitCode"
    }

    Write-Verbose "dswdevice.exe -i $XdpMpDeviceId $XdpMpComponentId"
    & $DswDevice -i $XdpMpDeviceId $XdpMpComponentId
    if ($LastExitCode) {
        Write-Error "devcon.exe exit code: $LastExitCode"
    }

    Wait-For-Adapters -IfDesc $XdpMpServiceName

    Write-Verbose "Renaming adapter"
    Rename-NetAdapter -InterfaceDescription $XdpMpServiceName $XdpMpServiceName

    Write-Verbose "Get-NetAdapter $XdpMpServiceName"
    Get-NetAdapter $XdpMpServiceName
    $AdapterIndex = (Get-NetAdapter $XdpMpServiceName).ifIndex

    Write-Verbose "Setting up the adapter"

    # NDIS polling has known race conditions and hangs on 2022 LTSC, so default
    # to the more reliable FNDIS.
    Set-NetAdapterAdvancedProperty -Name $XdpMpServiceName -RegistryKeyword PollProvider -DisplayValue "FNDIS"

    netsh.exe int ipv4 set int $AdapterIndex dadtransmits=0
    netsh.exe int ipv4 add address $AdapterIndex address=192.168.100.1/24
    netsh.exe int ipv4 add neighbor $AdapterIndex address=192.168.100.2 neighbor=22-22-22-22-00-02

    netsh.exe int ipv6 set int $AdapterIndex dadtransmits=0
    netsh.exe int ipv6 add address $AdapterIndex address=fc00::100:1/112
    netsh.exe int ipv6 add neighbor $AdapterIndex address=fc00::100:2 neighbor=22-22-22-22-00-02

    Write-Verbose "Adding firewall rules"
    netsh.exe advfirewall firewall add rule name="Allow$($XdpMpServiceName)v4" dir=in action=allow protocol=any remoteip=192.168.100.0/24
    netsh.exe advfirewall firewall add rule name="Allow$($XdpMpServiceName)v6" dir=in action=allow protocol=any remoteip=fc00::100:0/112

    Write-Verbose "Set-NetAdapterDataPathConfiguration -Name $XdpMpServiceName -Profile Passive"
    Set-NetAdapterDataPathConfiguration -Name $XdpMpServiceName -Profile Passive

    Write-Verbose "xdpmp.sys install complete!"
}

# Uninstalls the xdpmp driver.
function Uninstall-XdpMp {
    Write-Host "------- Uninstalling xdpmp.sys -------"

    netsh.exe advfirewall firewall del rule name="Allow$($XdpMpServiceName)v4"
    netsh.exe advfirewall firewall del rule name="Allow$($XdpMpServiceName)v6"

    try { & $DswDevice -u $XdpMpDeviceId } catch { Write-Verbose "Deleting $XdpMpDeviceId device failed" }

    Cleanup-Service $XdpMpServiceName

    Uninstall-Driver "xdpmp.inf"

    if ($Verifier) {
        try { verifier.exe /volatile /removedriver xdpmp.sys > $null } catch { Write-Verbose "verifier (removal) failure" }
    }

    Write-Verbose "xdpmp.sys uninstall complete!"
}

# Installs the xdpfnmp driver.
function Install-XdpFnMp {
    Write-Host "+++++++ Installing xdpfnmp.sys +++++++"

    if (!(Test-Path $XdpFnMpSys)) {
        Write-Error "$XdpFnMpSys does not exist!"
    }

    Write-Verbose "pnputil.exe /install /add-driver $XdpFnMpInf"
    pnputil.exe /install /add-driver $XdpFnMpInf
    if ($LastExitCode) {
        Write-Error "pnputil.exe exit code: $LastExitCode"
    }

    Write-Verbose "dswdevice.exe -i $XdpFnMpDeviceId0 $XdpFnMpComponentId"
    & $DswDevice -i $XdpFnMpDeviceId0 $XdpFnMpComponentId
    if ($LastExitCode) {
        Write-Error "devcon.exe exit code: $LastExitCode"
    }
    Write-Verbose "dswdevice.exe -i $XdpFnMpDeviceId1 $XdpFnMpComponentId"
    & $DswDevice -i $XdpFnMpDeviceId1 $XdpFnMpComponentId
    if ($LastExitCode) {
        Write-Error "devcon.exe exit code: $LastExitCode"
    }

    Wait-For-Adapters -IfDesc $XdpFnMpServiceName -Count 2

    Write-Verbose "Renaming adapters"
    Rename-NetAdapter -InterfaceDescription XDPFNMP XDPFNMP
    Rename-NetAdapter -InterfaceDescription "XDPFNMP #2" XDPFNMP1Q

    Write-Verbose "Get-NetAdapter XDPFNMP"
    Get-NetAdapter XDPFNMP
    Write-Verbose "Get-NetAdapter XDPFNMP1Q"
    Get-NetAdapter XDPFNMP1Q

    Write-Verbose "Set-NetAdapterRss -Name XDPFNMP1Q -NumberOfReceiveQueues 1"
    Set-NetAdapterRss -Name XDPFNMP1Q -NumberOfReceiveQueues 1

    Write-Verbose "Configure xdpfnmp ipv4"
    netsh int ipv4 set int interface=xdpfnmp dadtransmits=0
    netsh int ipv4 add address name=xdpfnmp address=192.168.200.1/24
    netsh int ipv4 add neighbor xdpfnmp address=192.168.200.2 neighbor=22-22-22-22-00-02

    Write-Verbose "Configure xdpfnmp ipv6"
    netsh int ipv6 set int interface=xdpfnmp dadtransmits=0
    netsh int ipv6 add address interface=xdpfnmp address=fc00::200:1/112
    netsh int ipv6 add neighbor xdpfnmp address=fc00::200:2 neighbor=22-22-22-22-00-02

    Write-Verbose "Configure xdpfnmp1q ipv4"
    netsh int ipv4 set int interface=xdpfnmp1q dadtransmits=0
    netsh int ipv4 add address name=xdpfnmp1q address=192.168.201.1/24
    netsh int ipv4 add neighbor xdpfnmp1q address=192.168.201.2 neighbor=22-22-22-22-00-02

    Write-Verbose "Configure xdpfnmp1q ipv6"
    netsh int ipv6 set int interface=xdpfnmp1q dadtransmits=0
    netsh int ipv6 add address interface=xdpfnmp1q address=fc00::201:1/112
    netsh int ipv6 add neighbor xdpfnmp1q address=fc00::201:2 neighbor=22-22-22-22-00-02

    Write-Verbose "xdpfnmp.sys install complete!"
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

    try { & $DswDevice -u $XdpFnMpDeviceId1 } catch { Write-Verbose "Deleting $XdpMpDeviceId1 device failed" }
    try { & $DswDevice -u $XdpFnMpDeviceId0 } catch { Write-Verbose "Deleting $XdpMpDeviceId0 device failed" }

    Cleanup-Service $XdpFnMpServiceName

    Uninstall-Driver "xdpfnmp.inf"

    Write-Verbose "xdpfnmp.sys uninstall complete!"
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
