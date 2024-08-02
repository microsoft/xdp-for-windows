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

.PARAMETER EnableEbpf
    Enable eBPF in the XDP driver.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fndis", "xdp", "xdpmp", "fnmp", "fnlwf", "ebpf")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "fndis", "xdp", "xdpmp", "fnmp", "fnlwf", "ebpf")]
    [string]$Uninstall = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("NDIS", "FNDIS")]
    [string]$XdpmpPollProvider = "NDIS",

    [Parameter(Mandatory = $false)]
    [ValidateSet("MSI", "INF")]
    [string]$XdpInstaller = "MSI",

    [Parameter(Mandatory = $false)]
    [switch]$EnableEbpf = $false
)

Set-StrictMode -Version 'Latest'
$OriginalErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

# Important paths.
$ArtifactsDir = "$RootDir\artifacts\bin\$($Arch)_$($Config)"
$LogsDir = "$RootDir\artifacts\logs"
$DevCon = Get-CoreNetCiArtifactPath -Name "devcon.exe"
$DswDevice = Get-CoreNetCiArtifactPath -Name "dswdevice.exe"

# File paths.
$XdpInf = "$ArtifactsDir\xdp\xdp.inf"
$XdpPcwMan = "$ArtifactsDir\xdppcw.man"
$XdpMsiFullPath = "$ArtifactsDir\xdpinstaller\xdp-for-windows.$(Get-XdpBuildVersionString).msi"
$FndisSys = "$ArtifactsDir\fndis\fndis.sys"
$XdpMpSys = "$ArtifactsDir\xdpmp\xdpmp.sys"
$XdpMpInf = "$ArtifactsDir\xdpmp\xdpmp.inf"
$XdpMpComponentId = "ms_xdpmp"
$XdpMpDeviceId = "xdpmp0"
$XdpMpServiceName = "XDPMP"

# Ensure the output path exists.
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

# Helper to capture failure diagnostics and trigger CI agent reboot
function Uninstall-Failure($FileName) {
    Collect-LiveKD -OutFile $LogsDir\$FileName

    Write-Host "##vso[task.setvariable variable=NeedsReboot]true"
    Write-Error "Uninstall failed"
}

# Helper to start (with retry) a service.
function Start-Service-With-Retry($Name) {
    Write-Verbose "Start-Service $Name"
    $StartSuccess = $false
    for ($i=0; $i -lt 100; $i++) {
        try {
            Start-Sleep -Milliseconds 10
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
            Uninstall-Failure "cleanup_service_$Name.dmp"
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
    if ($XdpInstaller -eq "MSI") {
        $XdpPath = Get-XdpInstallPath

        Write-Verbose "msiexec.exe /i $XdpMsiFullPath INSTALLFOLDER=$XdpPath /quiet /l*v $LogsDir\xdpinstall.txt"
        msiexec.exe /i $XdpMsiFullPath INSTALLFOLDER=$XdpPath /quiet /l*v $LogsDir\xdpinstall.txt | Write-Verbose

        if ($LastExitCode -ne 0) {
            Write-Error "XDP MSI installation failed: $LastExitCode"
        }
    } elseif ($XdpInstaller -eq "INF") {
        Write-Verbose "netcfg.exe -v -l $XdpInf -c s -i ms_xdp"
        netcfg.exe -v -l $XdpInf -c s -i ms_xdp | Write-Verbose
        if ($LastExitCode) {
            Write-Error "netcfg.exe exit code: $LastExitCode"
        }

        Write-Verbose "lodctr.exe /m:$XdpPcwMan $env:WINDIR\system32\drivers\"
        lodctr.exe /m:$XdpPcwMan $env:WINDIR\system32\drivers\ | Write-Verbose
        if ($LastExitCode) {
            Write-Error "lodctr.exe exit code: $LastExitCode"
        }
    }

    if ($EnableEbpf) {
        Write-Verbose "reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpEbpfEnabled /d 1 /t REG_DWORD /f"
        reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpEbpfEnabled /d 1 /t REG_DWORD /f | Write-Verbose
        Stop-Service xdp
    }

    Start-Service-With-Retry xdp

    Refresh-Path

    Write-Verbose "xdp.sys install complete!"
}

# Uninstalls the xdp driver.
function Uninstall-Xdp {
    if ($XdpInstaller -eq "MSI") {
        $XdpPath = Get-XdpInstallPath

        if (!(Test-Path $XdpPath)) {
            Write-Verbose "$XdpPath does not exist. Assuming XDP is not installed."
            return
        }

        Write-Verbose "msiexec.exe /x $XdpMsiFullPath /quiet /l*v $LogsDir\xdpuninstall.txt"
        msiexec.exe /x $XdpMsiFullPath /quiet /l*v $LogsDir\xdpuninstall.txt | Write-Verbose

        if ($LastExitCode -eq 0x666) {
            Write-Warning "The current version of XDP could not be uninstalled using MSI. Trying the existing installer..."

            $InstallId = (Get-CimInstance Win32_Product -Filter "Name = 'XDP for Windows'").IdentifyingNumber

            Write-Verbose "msiexec.exe /x $InstallId /quiet /l*v $LogsDir\xdpuninstallwmi.txt"
            msiexec.exe /x $InstallId /quiet /l*v $LogsDir\xdpuninstallwmi.txt
        }

        if ($LastExitCode -ne 0) {
            Write-Error "XDP MSI uninstall failed with status $LastExitCode" -ErrorAction Continue
            Uninstall-Failure "xdp_uninstall.dmp"
        }
    } elseif ($XdpInstaller -eq "INF") {
        Write-Verbose "unlodctr.exe /m:$XdpPcwMan"
        unlodctr.exe /m:$XdpPcwMan | Write-Verbose
        if ($LastExitCode) {
            Write-Error "unlodctr.exe exit code: $LastExitCode"
        }

        Write-Verbose "netcfg.exe -u ms_xdp"
        cmd.exe /c "netcfg.exe -u ms_xdp 2>&1" | Write-Verbose
        if (!$?) {
            Write-Host "netcfg.exe failed: $LastExitCode"
        }

        Uninstall-Driver "xdp.inf"

        Cleanup-Service xdp
    }

    Refresh-Path

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

    Write-Verbose "$DswDevice -u $XdpMpDeviceId"
    cmd.exe /c "$DswDevice -u $XdpMpDeviceId 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Deleting $XdpMpDeviceId device failed: $LastExitCode"
    }

    Write-Verbose "$DevCon remove @SWD\$XdpMpDeviceId\$XdpMpDeviceId"
    cmd.exe /c "$DevCon remove @SWD\$XdpMpDeviceId\$XdpMpDeviceId 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "Removing $XdpMpDeviceId device failed: $LastExitCode"
    }

    Cleanup-Service $XdpMpServiceName

    Uninstall-Driver "xdpmp.inf"

    Write-Verbose "xdpmp.sys uninstall complete!"
}

# Installs the fnmp driver.
function Install-FnMp {
    Write-Verbose "$(Get-FnRuntimeDir)/tools/setup.ps1 -Install fnmp -Config $Config -Arch $Arch -FnMpCount 2 -ArtifactsDir $(Get-FnRuntimeDir)/bin -LogsDir $LogsDir"
    & "$(Get-FnRuntimeDir)/tools/setup.ps1" -Install fnmp -Config $Config -Arch $Arch -FnMpCount 2 -ArtifactsDir "$(Get-FnRuntimeDir)/bin" -LogsDir $LogsDir

    Write-Verbose "Renaming adapters"
    Rename-NetAdapter-With-Retry FNMP XDPFNMP
    Rename-NetAdapter-With-Retry "FNMP #2" XDPFNMP1Q

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

    Write-Verbose "fnmp.sys install complete!"
}

# Uninstalls the fnmp driver.
function Uninstall-FnMp {
    netsh int ipv4 delete address xdpfnmp 192.168.200.1 | Out-Null
    netsh int ipv4 delete neighbors xdpfnmp | Out-Null
    netsh int ipv6 delete address xdpfnmp fc00::200:1 | Out-Null
    netsh int ipv6 delete neighbors xdpfnmp | Out-Null

    netsh int ipv4 delete address xdpfnmp1q 192.168.201.1 | Out-Null
    netsh int ipv4 delete neighbors xdpfnmp1q | Out-Null
    netsh int ipv6 delete address xdpfnmp1q fc00::201:1 | Out-Null
    netsh int ipv6 delete neighbors xdpfnmp1q | Out-Null

    Write-Verbose "$(Get-FnRuntimeDir)/tools/setup.ps1 -Uninstall fnmp -Config $Config -Arch $Arch -FnMpCount 2 -ArtifactsDir $(Get-FnRuntimeDir)/bin -LogsDir $LogsDir"
    & "$(Get-FnRuntimeDir)/tools/setup.ps1" -Uninstall fnmp -Config $Config -Arch $Arch -FnMpCount 2 -ArtifactsDir "$(Get-FnRuntimeDir)/bin" -LogsDir $LogsDir

    Write-Verbose "fnmp.sys uninstall complete!"
}

# Installs the fnlwf driver.
function Install-FnLwf {
    Write-Verbose "$(Get-FnRuntimeDir)/tools/setup.ps1 -Install fnlwf -Config $Config -Arch $Arch -ArtifactsDir $(Get-FnRuntimeDir)/bin -LogsDir $LogsDir"
    & "$(Get-FnRuntimeDir)/tools/setup.ps1" -Install fnlwf -Config $Config -Arch $Arch -ArtifactsDir "$(Get-FnRuntimeDir)/bin" -LogsDir $LogsDir
}

# Uninstalls the fnlwf driver.
function Uninstall-FnLwf {
    Write-Verbose "$(Get-FnRuntimeDir)/tools/setup.ps1 -Uninstall fnlwf -Config $Config -Arch $Arch -ArtifactsDir $(Get-FnRuntimeDir)/bin -LogsDir $LogsDir"
    & "$(Get-FnRuntimeDir)/tools/setup.ps1" -Uninstall fnlwf -Config $Config -Arch $Arch -ArtifactsDir "$(Get-FnRuntimeDir)/bin" -LogsDir $LogsDir
}

function Install-Ebpf {
    $EbpfPath = Get-EbpfInstallPath
    $EbpfMsiFullPath = Get-EbpfMsiFullPath
    $EbpfMsiFullPath = (Resolve-Path $EbpfMsiFullPath).Path

    if (Test-Path $EbpfPath) {
        Write-Error "$EbpfPath is already installed!"
    }

    # Try to install eBPF several times, since driver verifier's fault injection
    # may occasionally prevent the eBPF driver from loading.
    for ($i = 0; $i -lt 100; $i++) {
        Write-Verbose "msiexec.exe /i $EbpfMsiFullPath INSTALLFOLDER=$EbpfPath ADDLOCAL=eBPF_Runtime_Components_JIT /qn /l*v $LogsDir\ebpfinstall.txt"
        msiexec.exe /i $EbpfMsiFullPath INSTALLFOLDER=$EbpfPath ADDLOCAL=eBPF_Runtime_Components_JIT /qn /l*v $LogsDir\ebpfinstall.txt | Write-Verbose
        if ($?) {
            break;
        }
    }
    if (!$? -or !(Test-Path $EbpfPath)) {
        Write-Error "eBPF could not be installed"
    }
    # Stop eBPF's XDP hook since it conflicts with our XDP implementation.
    Stop-Service netebpfext
    Refresh-Path
}

function Uninstall-Ebpf {
    $EbpfPath = Get-EbpfInstallPath
    $EbpfMsiFullPath = Get-EbpfMsiFullPath
    $EbpfMsiFullPath = (Resolve-Path $EbpfMsiFullPath).Path
    $Timeout = 60

    if (!(Test-Path $EbpfPath)) {
        Write-Verbose "$EbpfPath does not exist. Assuming eBPF is not installed."
        return
    }

    Write-Verbose "Uninstalling eBPF for Windows"

    Write-Verbose "msiexec.exe /x $EbpfMsiFullPath /qn /l*v $LogsDir\ebpfuninstall.txt"
    $Process = Start-Process -FilePath msiexec.exe -NoNewWindow -PassThru -ArgumentList `
        @("/x", $EbpfMsiFullPath, "/qn", "/l*v", "$LogsDir\ebpfuninstall.txt")

    $Process | Wait-Process -Timeout $Timeout -ErrorAction Ignore

    if (!$Process.HasExited) {
        Write-Error "eBPF failed to uninstall within $Timeout seconds" -ErrorAction Continue
        Collect-ProcessDump -ProcessName "ebpfsvc.exe" -OutFile "$LogsDir\ebpf_uninstall_ebpfsvc.dmp"
        Collect-ProcessDump -ProcessName "msiexec.exe" -ProcessId $Process.Id -OutFile "$LogsDir\ebpf_uninstall_msiexec.dmp"
        Uninstall-Failure "ebpf_uninstall_timeout.dmp"
    }

    if ($Process.ExitCode -ne 0) {
        if (Process.ExitCode -eq 0x666) {
            Write-Error "An unexpected version of eBPF could not be uninstalled"
        } else {
            Write-Error "MSI uninstall failed with status $($Process.ExitCode)" -ErrorAction Continue
            Uninstall-Failure "ebpf_uninstall.dmp"
        }
    }

    if (Test-Path $EbpfPath) {
        Write-Error "eBPF could not be uninstalled"
    }
    Refresh-Path
}

try {
    if ($Install -eq "fndis") {
        Install-FakeNdis
    }
    if ($Install -eq "xdp") {
        Install-Xdp
    }
    if ($Install -eq "xdpmp") {
        Install-XdpMp
    }
    if ($Install -eq "fnmp") {
        Install-FnMp
    }
    if ($Install -eq "fnlwf") {
        Install-FnLwf
    }
    if ($Install -eq "ebpf") {
        Install-Ebpf
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
    if ($Uninstall -eq "fnmp") {
        Uninstall-FnMp
    }
    if ($Uninstall -eq "fnlwf") {
        Uninstall-FnLwf
    }
    if ($Uninstall -eq "ebpf") {
        Uninstall-Ebpf
    }
} catch {
    Write-Error $_ -ErrorAction $OriginalErrorActionPreference
}
