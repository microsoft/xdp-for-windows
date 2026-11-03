<#

.SYNOPSIS
This script installs or uninstalls various XDP components.

.PARAMETER Install
    Specifies an XDP component to install. If XDP is already installed, the
    "xdp" and "xdppa" components are upgraded in place: the existing
    installation is removed and the binaries from the binary directory are
    installed. If the XDP eBPF feature was enabled, it is re-enabled after the
    upgrade.

.PARAMETER Uninstall
    Attempts to uninstall all XDP components.

.PARAMETER BinaryDirectory
    Overrides the binary directory. The default directory is this script's
    directory.

#>

param (

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "xdp", "xdpebpfexport", "xdpebpf", "xdppa")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "xdp", "xdpebpfexport", "xdpebpf", "xdppa")]
    [string]$Uninstall = "",

    [Parameter(Mandatory = $false)]
    [string]$BinaryDirectory = "",

    # When set, treat normally-fatal cleanup errors during -Uninstall as
    # recoverable warnings.
    [Parameter(Mandatory = $false)]
    [switch]$Force = $false
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

if ([string]::IsNullOrWhitespace($BinaryDirectory)) {
    $script:InstallDir = $PSScriptRoot
} else {
    $script:InstallDir = $BinaryDirectory
}

# Global paths.
$XdpInf = "$InstallDir\xdp.inf"
$XdpPcwMan = "$InstallDir\xdppcw.man"
$XdpBpfExport = "$InstallDir\xdpbpfexport.exe"

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
            Write-Error "Failed to clean up $Name!"
        }
    }
}

function Cleanup-File($Path) {
    Write-Verbose "Cleaning up $Path"
    if (Test-Path $Path) {
        Write-Verbose "Remove-Item $Path -Force"
        Remove-Item $Path -Force
    } else {
        Write-Verbose "$Path did not exist"
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

function Install-XdpComponent($ComponentId) {
    if (!(Test-Path "$XdpInf")) {
        Write-Error "$XdpInf does not exist!"
    }

    Write-Verbose "netcfg.exe -v -l '$XdpInf' -c s -i ms_$ComponentId"
    netcfg.exe -v -l "$XdpInf" -c s -i ms_$ComponentId | Write-Verbose
    if ($LastExitCode) {
        Write-Error "netcfg.exe exit code: $LastExitCode"
    }

    Write-Verbose "lodctr.exe /m:$XdpPcwMan $env:WINDIR\system32\drivers\"
    lodctr.exe /m:$XdpPcwMan $env:WINDIR\system32\drivers\ | Write-Verbose
    if ($LastExitCode) {
        Write-Error "lodctr.exe exit code: $LastExitCode"
    }

    Write-Verbose "xdp.sys install complete!"
}

function Uninstall-XdpComponent($ComponentId) {
    Write-Verbose "unlodctr.exe /m:$XdpPcwMan"
    unlodctr.exe /m:$XdpPcwMan | Write-Verbose
    if ($LastExitCode) {
        Write-Error "unlodctr.exe exit code: $LastExitCode"
    }

    Write-Verbose "netcfg.exe -u ms_$ComponentId"
    cmd.exe /c "netcfg.exe -u ms_$ComponentId 2>&1" | Write-Verbose
    if (!$?) {
        Write-Host "netcfg.exe failed: $LastExitCode"
    }

    Uninstall-Driver "xdp.inf"

    Cleanup-Service xdp

    Cleanup-File $env:WINDIR\system32\xdpapi.dll
    Cleanup-File $env:WINDIR\system32\drivers\xdp.sys

    Write-Verbose "xdp.sys uninstall complete!"
}

# Returns the component ID ("xdp" or "xdp_pa") of the currently installed XDP
# component, or an empty string if XDP is not installed.
function Get-InstalledXdpComponent {
    if (Get-NetAdapterBinding -ComponentID ms_xdp_pa -ErrorAction Ignore) {
        return "xdp_pa"
    }
    if ((Get-NetAdapterBinding -ComponentID ms_xdp -ErrorAction Ignore) -or
        (Get-Service xdp -ErrorAction Ignore)) {
        return "xdp"
    }
    return ""
}

# Returns whether the XDP eBPF feature is enabled in the xdp service registry.
function Test-XdpEbpfEnabled {
    $Params = Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\xdp\Parameters" -ErrorAction Ignore
    if (!$Params -or !$Params.PSObject.Properties["XdpEbpfEnabled"]) {
        return $false
    }
    return $Params.XdpEbpfEnabled -ne 0
}

# Installs the XDP eBPF feature.
function Install-XdpEbpf {
    Write-Verbose "reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpEbpfEnabled /d 1 /t REG_DWORD /f"
    reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpEbpfEnabled /d 1 /t REG_DWORD /f | Write-Verbose

    # XDP needs to be restarted to reload this registry key. Continue past failures.
    Write-Verbose "Restarting xdp"
    Restart-Service xdp -ErrorAction 'Continue'

    Write-Verbose "$XdpBpfExport"
    & $XdpBpfExport
    if ($LastExitCode) {
        Write-Error "$XdpBpfExport exit code: $LastExitCode"
    }

    Write-Verbose "XDP eBPF feature install complete!"
}

# Installs the given XDP component. If XDP is already installed, the existing
# installation is removed first so the binaries are replaced (upgraded), and the
# eBPF feature is re-enabled afterwards if it was previously enabled.
function Install-Xdp($ComponentId) {
    $InstalledComponentId = Get-InstalledXdpComponent

    if ([string]::IsNullOrEmpty($InstalledComponentId)) {
        Install-XdpComponent $ComponentId
        return
    }

    $EbpfEnabled = Test-XdpEbpfEnabled
    Write-Verbose "XDP component ms_$InstalledComponentId is already installed (eBPF enabled: $EbpfEnabled); replacing it with ms_$ComponentId"

    try {
        Uninstall-XdpComponent $InstalledComponentId
    } catch {
        Write-Error "Failed to remove the existing XDP installation, so XDP cannot be upgraded in place: $_ Close any open XDP handles (for example, AF_XDP sockets and XDP programs), or run 'xdp-setup.ps1 -Uninstall xdp' and reboot, and then try again."
    }

    Install-XdpComponent $ComponentId

    if ($EbpfEnabled) {
        Write-Verbose "Re-enabling the XDP eBPF feature"
        Install-XdpEbpf
    }
}

# Installs (or upgrades) the xdp driver.
if ($Install -eq "xdp") {
    Install-Xdp "xdp"
}

# Installs (or upgrades) the xdp driver in PA mode. Implicitly uninstalls
# regular xdp.
if ($Install -eq "xdppa") {
    Install-Xdp "xdp_pa"
}

# Uninstalls the xdp driver from PA mode. Implicitly installs regular xdp.
if ($Uninstall -eq "xdppa") {
    Uninstall-XdpComponent "xdp_pa"
    Install-XdpComponent "xdp"
}

# Uninstalls the xdp driver.
if ($Uninstall -eq "xdp") {
    Uninstall-XdpComponent "xdp"
}

# Installs the XDP eBPF feature.
if ($Install -eq "xdpebpf") {
    Install-XdpEbpf
}

# Uninstalls the XDP eBPF feature.
if ($Uninstall -eq "xdpebpf") {
    Write-Verbose "$XdpBpfExport --clear"
    # Use cmd.exe to insulate from PowerShell 7+ converting native-command
    # stderr into terminating errors (which would bypass the explicit
    # -Force exit-code check below).
    cmd.exe /c "`"$XdpBpfExport`" --clear 2>&1" | Write-Verbose
    if ($LastExitCode) {
        if ($Force) {
            Write-Warning "$XdpBpfExport --clear exit code: $LastExitCode (ignored due to -Force)"
            $global:LASTEXITCODE = 0
        } else {
            Write-Error "$XdpBpfExport exit code: $LastExitCode"
        }
    }

    Write-Verbose "reg.exe delete HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpEbpfEnabled /f"
    reg.exe delete HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v XdpEbpfEnabled /f | Write-Verbose

    # XDP needs to be restarted to reload this registry key. Continue past failures.
    Write-Verbose "Restarting xdp"
    Restart-Service xdp -ErrorAction 'Continue'

    Write-Verbose "XDP eBPF feature uninstall complete!"
}

if ($Install -eq "xdpebpfexport") {
    Write-Verbose "$XdpBpfExport"
    & $XdpBpfExport
    if ($LastExitCode) {
        Write-Error "$XdpBpfExport exit code: $LastExitCode"
    }

    Write-Verbose "XDP eBPF export install complete!"
}

if ($Uninstall -eq "xdpebpfexport") {
    Write-Verbose "$XdpBpfExport --clear"
    & $XdpBpfExport --clear
    if ($LastExitCode) {
        Write-Error "$XdpBpfExport --clear exit code: $LastExitCode"
    }

    Write-Verbose "XDP eBPF export uninstall complete!"
}
