# Copyright (c) eBPF for Windows contributors
# SPDX-License-Identifier: MIT
#
# Adapted from eBPF for Windows' Use-PrivateEbpfStack.ps1 and maintained as part
# of XDP's test tooling; it intentionally diverges from upstream.

<#

.SYNOPSIS
Temporarily replaces a preinstalled eBPF for Windows stack with a private one.

.DESCRIPTION
Enable records the preinstalled eBPF configuration, copies a staged eBPF
runtime into PrivateDirectory, and points the eBPF services, eBPF registry
store, netsh helper and machine PATH at the private copy before starting it.

Restore undoes every change made by Enable (including a partially completed
Enable), deletes PrivateDirectory, and restarts the eBPF services that were
running beforehand. Restore outputs $true if it restored a private stack and
$false if none was active.

.EXAMPLE
.\Use-PrivateEbpfStack.ps1 Enable `
    -ArtifactDirectory artifacts\ebpfprivate\x64.1.4.0 `
    -PrivateDirectory "C:\Program Files\XdpPrivateEbpfStack"

.EXAMPLE
.\Use-PrivateEbpfStack.ps1 Restore

#>

param (
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("Enable", "Restore")]
    [string]$Action,

    # Directory containing the staged eBPF runtime files. Required for Enable.
    [Parameter(Mandatory = $false)]
    [string]$ArtifactDirectory = "",

    # Directory the private stack is run from. Must not already exist; it is
    # deleted by Restore. Required for Enable.
    [Parameter(Mandatory = $false)]
    [string]$PrivateDirectory = ""
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$ServiceBinaries = [ordered]@{
    EbpfCore = "EbpfCore.sys"
    NetEbpfExt = "NetEbpfExt.sys"
    eBPFSvc = "EbpfSvc.exe"
}
$StateRoot = Join-Path $env:ProgramData "XdpPrivateEbpfStack"
$StatePath = Join-Path $StateRoot "state.json"
$BackupPath = Join-Path $StateRoot "backup"
$NetshRegistryPath = "HKLM:\SOFTWARE\Microsoft\NetSh"
$EbpfStoreKeys = @("HKLM\Software\eBPF", "HKCU\Software\eBPF")

# Quotes an argument using the rules native programs use to split command lines.
function Format-NativeArgument($Value) {
    return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

# Windows PowerShell drops embedded quotes when calling native programs (e.g. a
# quoted service binPath), so build the command line explicitly.
function Invoke-NativeCommand {
    param (
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$ArgumentList,

        [Parameter(Mandatory = $false)]
        [string]$WorkingDirectory = $PWD.ProviderPath
    )

    $StartArgs = @{
        FilePath = $FilePath
        WorkingDirectory = $WorkingDirectory
        NoNewWindow = $true
        Wait = $true
        PassThru = $true
    }
    $CommandLine = ($ArgumentList | ForEach-Object { Format-NativeArgument $_ }) -join " "
    if ($CommandLine) {
        $StartArgs.ArgumentList = $CommandLine
    }
    Write-Verbose "$FilePath $CommandLine"
    $Output = New-TemporaryFile
    try {
        $Process = Start-Process @StartArgs -RedirectStandardOutput $Output.FullName
        Get-Content $Output.FullName | Write-Verbose
    } finally {
        Remove-Item $Output.FullName
    }
    if ($Process.ExitCode -ne 0) {
        Write-Error "'$FilePath $CommandLine' failed with exit code $($Process.ExitCode)."
    }
}

function Get-ServiceConfiguration($Name) {
    $Service = Get-Service -Name $Name -ErrorAction SilentlyContinue
    if ($null -eq $Service) {
        Write-Error "Required service '$Name' is not installed."
    }

    $Values = Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\$Name"
    [PSCustomObject]@{
        Name = $Name
        ImagePath = [string]$Values.ImagePath
        Start = [int]$Values.Start
        DelayedAutoStart =
            if ($null -ne $Values.PSObject.Properties["DelayedAutoStart"]) {
                [int]$Values.DelayedAutoStart
            } else {
                $null
            }
        WasRunning = $Service.Status -eq [System.ServiceProcess.ServiceControllerStatus]::Running
    }
}

function Set-ServiceConfiguration($Name, $ImagePath, [int]$Start) {
    $StartName = switch ($Start) {
        0 { "boot" }
        1 { "system" }
        2 { "auto" }
        3 { "demand" }
        4 { "disabled" }
        default { Write-Error "Unsupported service Start value '$Start'." }
    }
    Invoke-NativeCommand -FilePath "$env:SystemRoot\System32\sc.exe" `
        -ArgumentList @("config", $Name, "binPath=", $ImagePath, "start=", $StartName)
}

function Stop-EbpfStack {
    # Stop dependents first.
    foreach ($Name in @("eBPFSvc", "NetEbpfExt", "EbpfCore")) {
        $Service = Get-Service -Name $Name
        if ($Service.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Stopped) {
            Write-Verbose "Stop-Service $Name"
            Stop-Service -Name $Name -Force
            $Service.WaitForStatus(
                [System.ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(60))
        }
    }
}

function ConvertTo-RegistryProviderPath($RegistryPath) {
    if ($RegistryPath -like "HKLM\*") {
        return "Registry::HKEY_LOCAL_MACHINE\" + $RegistryPath.Substring(5)
    }
    if ($RegistryPath -like "HKCU\*") {
        return "Registry::HKEY_CURRENT_USER\" + $RegistryPath.Substring(5)
    }
    Write-Error "Unsupported registry path '$RegistryPath'."
}

function Get-RegistryBackupFile($RegistryPath) {
    return Join-Path $BackupPath (($RegistryPath -replace '\\', '_') + ".reg")
}

# Exports the key to the backup directory and returns whether it existed.
function Backup-RegistryKey($RegistryPath) {
    if (!(Test-Path -LiteralPath (ConvertTo-RegistryProviderPath $RegistryPath))) {
        return $false
    }
    Invoke-NativeCommand -FilePath "$env:SystemRoot\System32\reg.exe" `
        -ArgumentList @("export", $RegistryPath, (Get-RegistryBackupFile $RegistryPath), "/y")
    return $true
}

function Restore-RegistryKey($RegistryPath, [bool]$Existed) {
    $ProviderPath = ConvertTo-RegistryProviderPath $RegistryPath
    if (Test-Path -LiteralPath $ProviderPath) {
        Remove-Item -LiteralPath $ProviderPath -Recurse -Force
    }
    if ($Existed) {
        Invoke-NativeCommand -FilePath "$env:SystemRoot\System32\reg.exe" `
            -ArgumentList @("import", (Get-RegistryBackupFile $RegistryPath))
    }
}

# Returns the registered netsh helpers that implement "netsh ebpf". Some images
# register the inbox helper under a different file name (e.g. ebpfnetsh_prev.dll).
function Get-EbpfNetshHelpers {
    $Key = Get-Item -Path $NetshRegistryPath
    return @(
        $Key.GetValueNames() |
            Where-Object { [IO.Path]::GetFileName([string]$Key.GetValue($_)) -like "ebpfnetsh*.dll" } |
            ForEach-Object { [PSCustomObject]@{ Name = $_; Value = [string]$Key.GetValue($_) } }
    )
}

function Set-EbpfNetshHelpers($Helpers) {
    foreach ($Helper in Get-EbpfNetshHelpers) {
        Write-Verbose "Unregistering netsh helper $($Helper.Name)=$($Helper.Value)"
        Remove-ItemProperty -Path $NetshRegistryPath -Name $Helper.Name
    }
    foreach ($Helper in $Helpers) {
        Write-Verbose "Registering netsh helper $($Helper.Name)=$($Helper.Value)"
        New-ItemProperty -Path $NetshRegistryPath -Name $Helper.Name -Value $Helper.Value `
            -PropertyType String -Force | Out-Null
    }
}

function Get-MachinePathEntries {
    return @([Environment]::GetEnvironmentVariable("Path", "Machine") -split ";" | Where-Object { $_ })
}

function Test-SamePath($Left, $Right) {
    return $Left.TrimEnd("\") -eq $Right.TrimEnd("\")
}

function Add-MachinePathEntry($Directory) {
    $Entries = Get-MachinePathEntries
    if (!($Entries | Where-Object { Test-SamePath $_ $Directory })) {
        [Environment]::SetEnvironmentVariable("Path", (($Entries + $Directory) -join ";"), "Machine")
    }
    Refresh-Path
}

function Remove-MachinePathEntry($Directory) {
    $Entries = Get-MachinePathEntries
    $Remaining = @($Entries | Where-Object { !(Test-SamePath $_ $Directory) })
    if ($Remaining.Count -ne $Entries.Count) {
        [Environment]::SetEnvironmentVariable("Path", ($Remaining -join ";"), "Machine")
    }
    Refresh-Path
}

# The private stack contains drivers and a service binary, so only privileged
# accounts may modify it.
function Set-PrivateDirectoryAcl($Path) {
    $Acl = Get-Acl -Path $Path
    $Acl.SetAccessRuleProtection($true, $false)
    foreach ($Rule in @($Acl.Access)) {
        $Acl.RemoveAccessRuleSpecific($Rule) | Out-Null
    }

    $Inherit =
        [System.Security.AccessControl.InheritanceFlags]::ContainerInherit -bor
        [System.Security.AccessControl.InheritanceFlags]::ObjectInherit
    $Propagate = [System.Security.AccessControl.PropagationFlags]::None
    $Allow = [System.Security.AccessControl.AccessControlType]::Allow
    $Grants = @(
        @("BUILTIN\Administrators", "FullControl"),
        @("NT AUTHORITY\SYSTEM", "ReadAndExecute"),
        @("NT SERVICE\eBPFSvc", "ReadAndExecute"),
        @("BUILTIN\Users", "ReadAndExecute")
    )
    foreach ($Grant in $Grants) {
        $Acl.AddAccessRule([System.Security.AccessControl.FileSystemAccessRule]::new(
            $Grant[0], $Grant[1], $Inherit, $Propagate, $Allow))
    }
    Set-Acl -Path $Path -AclObject $Acl
}

function Copy-PrivateStack($Source, $Destination) {
    New-Item -ItemType Directory -Path $Destination | Out-Null
    foreach ($File in Get-PrivateEbpfStackFiles) {
        Copy-Item -Path (Join-Path $Source $File) -Destination $Destination
    }
    # Keep runtime dependencies (e.g. the CRT) app-local.
    Get-ChildItem -Path $Source -File |
        Where-Object { $_.Extension -in @(".dll", ".pdb") } |
        Copy-Item -Destination $Destination -Force
    Set-PrivateDirectoryAcl $Destination
}

function Enable-PrivateStack {
    if (!$ArtifactDirectory -or !$PrivateDirectory) {
        Write-Error "-ArtifactDirectory and -PrivateDirectory are required for Enable."
    }
    if (Test-Path $StatePath) {
        Write-Error "A private eBPF stack is already active. Run '$PSCommandPath Restore' first."
    }
    $Source = (Resolve-Path -Path $ArtifactDirectory).Path
    $Destination = [IO.Path]::GetFullPath($PrivateDirectory)
    if (Test-Path $Destination) {
        Write-Error "PrivateDirectory '$Destination' already exists."
    }
    foreach ($File in Get-PrivateEbpfStackFiles) {
        if (!(Test-Path -Path (Join-Path $Source $File) -PathType Leaf)) {
            Write-Error "Required file '$File' was not found in '$Source'."
        }
    }

    # Record the original configuration before changing anything so Restore
    # can undo a partially completed Enable.
    if (Test-Path $BackupPath) {
        Remove-Item -Recurse -Force $BackupPath
    }
    New-Item -ItemType Directory -Path $BackupPath | Out-Null
    $State = [PSCustomObject]@{
        PrivateDirectory = $Destination
        Services = @($ServiceBinaries.Keys | ForEach-Object { Get-ServiceConfiguration $_ })
        EbpfStoreKeys = @($EbpfStoreKeys | ForEach-Object {
            [PSCustomObject]@{ Path = $_; Existed = Backup-RegistryKey $_ }
        })
        NetshHelpers = @(Get-EbpfNetshHelpers)
    }
    # Write the state atomically: Restore treats an existing state file as an
    # active (possibly partially enabled) private stack.
    $State | ConvertTo-Json -Depth 4 | Set-Content -Path "$StatePath.tmp" -Encoding UTF8
    Move-Item -Path "$StatePath.tmp" -Destination $StatePath -Force

    Write-Verbose "Enabling private eBPF stack from $Destination"
    Stop-EbpfStack
    Copy-PrivateStack $Source $Destination
    foreach ($Name in $ServiceBinaries.Keys) {
        $ImagePath = Join-Path $Destination $ServiceBinaries[$Name]
        # Win32 service image paths are command lines and must be quoted;
        # kernel drivers load the image path literally and must not be.
        if ((Get-Service -Name $Name).ServiceType -notmatch "Driver") {
            $ImagePath = '"' + $ImagePath + '"'
        }
        Set-ServiceConfiguration $Name $ImagePath 3
    }

    # Replace the eBPF registry store with the private stack's program info.
    $ExportProgramInfo = Join-Path $Destination "export_program_info.exe"
    Invoke-NativeCommand -FilePath $ExportProgramInfo -ArgumentList @("--clear") -WorkingDirectory $Destination
    Invoke-NativeCommand -FilePath $ExportProgramInfo -ArgumentList @() -WorkingDirectory $Destination

    Set-EbpfNetshHelpers @([PSCustomObject]@{
        Name = "ebpfnetsh"; Value = (Join-Path $Destination "ebpfnetsh.dll")
    })
    # netsh loads helpers with the default DLL search order, so the private
    # ebpfnetsh.dll can only resolve the private EbpfApi.dll through PATH.
    Add-MachinePathEntry $Destination

    Start-EbpfServices
}

function Restore-PreinstalledStack {
    if (!(Test-Path $StatePath)) {
        Write-Verbose "No private eBPF stack is active."
        return $false
    }
    $State = Get-Content -Path $StatePath -Raw | ConvertFrom-Json

    Write-Verbose "Restoring preinstalled eBPF stack"
    Stop-EbpfStack
    foreach ($Service in $State.Services) {
        Set-ServiceConfiguration $Service.Name $Service.ImagePath $Service.Start
        $ServiceKey = "HKLM:\SYSTEM\CurrentControlSet\Services\$($Service.Name)"
        if ($null -eq $Service.DelayedAutoStart) {
            Remove-ItemProperty -Path $ServiceKey -Name "DelayedAutoStart" -ErrorAction Ignore
        } else {
            Set-ItemProperty -Path $ServiceKey -Name "DelayedAutoStart" -Value ([int]$Service.DelayedAutoStart)
        }
    }
    foreach ($Key in $State.EbpfStoreKeys) {
        Restore-RegistryKey $Key.Path $Key.Existed
    }
    Set-EbpfNetshHelpers @($State.NetshHelpers | Where-Object { $_ })
    Remove-MachinePathEntry $State.PrivateDirectory
    if (Test-Path $State.PrivateDirectory) {
        Remove-Item -Recurse -Force $State.PrivateDirectory
    }

    foreach ($Service in $State.Services) {
        if ($Service.WasRunning) {
            Start-Service-With-Retry $Service.Name
        }
    }

    # Delete the state last so that a failed Restore can be retried.
    Remove-Item -Recurse -Force $StateRoot
    Write-Verbose "Preinstalled eBPF stack restored."
    return $true
}

if (!(Test-Admin) -and ($Action -eq "Enable" -or (Test-Path $StatePath))) {
    Write-Error "This script must be run from an elevated PowerShell session."
}

switch ($Action) {
    "Enable" { Enable-PrivateStack }
    "Restore" { Restore-PreinstalledStack }
}
