<#

.SYNOPSIS
This script installs or uninstalls various XDP components.

.PARAMETER Install
    Specifies an XDP component to install.

.PARAMETER Uninstall
    Attempts to uninstall all XDP components.

#>

param (
    
    [Parameter(Mandatory = $false)]
    [ValidateSet("", "xdp")]
    [string]$Install = "",

    [Parameter(Mandatory = $false)]
    [ValidateSet("", "xdp")]
    [string]$Uninstall = ""
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Global paths.
$SystemFolder = [Environment]::SystemDirectory
$XdpSys = "$SystemFolder\drivers\xdp.sys"
$XdpInf = "$SystemFolder\drivers\xdp.inf"
$LogsDir = "$SystemFolder\Logs"

# Set the temporary working directory and testore it on exit.
Push-Location -Path $SystemFolder

# Helper to reboot the machine.
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

# Installs the xdp driver.
if ($Install -eq "xdp") {
    Install-Xdp    
}

# Uninstalls the xdp driver.
if ($Uninstall -eq "xdp") {
    Uninstall-Xdp
}

Pop-Location