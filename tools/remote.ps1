<#

.SYNOPSIS
Helpers for running the XDP test scripts on a remote test machine over
PowerShell remoting (WinRM).

.DESCRIPTION
This script provides three modes that together make running the existing
XDP test scripts (functional.ps1, spinxsk.ps1, ...) against a remote test
machine seamless:

  -EnableRemoting   Run ON THE TEST MACHINE (admin) to enable WinRM. This
                    is a one-time setup. Equivalent to running:
                        Enable-PSRemoting -Force -SkipNetworkProfileCheck

  -Deploy           Run from the dev machine to copy the local 'tools' and
                    relevant 'artifacts' folders to the remote machine.
                    Uses a single compressed archive over the WinRM session
                    so transfer is fast. Re-running is safe and only
                    overwrites files.

  -Invoke           Run from the dev machine to execute one of the XDP
                    PowerShell scripts on the remote machine. Output,
                    verbose, warning, and error streams are forwarded back.

This script is also dot-source friendly: helper functions
(Connect-XdpRemote, Deploy-XdpRemote, Invoke-XdpRemote) are exported so
other scripts (e.g. functional.ps1) can integrate the -ComputerName
workflow directly.

.PARAMETER ComputerName
The remote test machine. Accepts hostname, FQDN, or IP. When using an IP
or non-domain hostname, ensure the client's TrustedHosts list includes it
or supply an explicit -Credential authenticated via Kerberos.

.PARAMETER Credential
Optional PSCredential for authenticating to the remote machine.

.PARAMETER RemoteRoot
Destination directory on the remote machine. Defaults to C:\xdp.
The XDP scripts on the remote will run with this directory as their
workspace root, mirroring the local layout (tools\, artifacts\, src\).

.PARAMETER ScriptPath
For -Invoke: path (relative to repo root) of the script to run on the
remote, e.g. 'tools\functional.ps1'.

.PARAMETER ArgumentList
For -Invoke: hashtable of parameters splatted to the remote script.

.PARAMETER SkipArtifacts
For -Deploy: only copy tools\ and src\xdp.props, skip the (large)
artifacts\bin\... payload. Useful for iterating on scripts only.

.EXAMPLE
# One-time, on the test machine (elevated PowerShell):
.\tools\remote.ps1 -EnableRemoting

.EXAMPLE
# From the dev box:
.\tools\remote.ps1 -Deploy -ComputerName test-vm-1
.\tools\functional.ps1 -ComputerName test-vm-1

.EXAMPLE
# Run a one-off remote command:
.\tools\remote.ps1 -Invoke -ComputerName test-vm-1 -ScriptPath tools\functional.ps1 `
    -ArgumentList @{ TestCaseFilter = 'Name=GenericBinding' }

#>

[CmdletBinding(DefaultParameterSetName = 'None')]
param (
    [Parameter(Mandatory = $true, ParameterSetName = 'EnableRemoting')]
    [switch]$EnableRemoting,

    [Parameter(Mandatory = $true, ParameterSetName = 'Deploy')]
    [switch]$Deploy,

    [Parameter(Mandatory = $true, ParameterSetName = 'Invoke')]
    [switch]$Invoke,

    [Parameter(Mandatory = $true, ParameterSetName = 'Deploy')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Invoke')]
    [string]$ComputerName,

    [Parameter(ParameterSetName = 'Deploy')]
    [Parameter(ParameterSetName = 'Invoke')]
    [System.Management.Automation.PSCredential]$Credential,

    [Parameter(ParameterSetName = 'Deploy')]
    [Parameter(ParameterSetName = 'Invoke')]
    [string]$RemoteRoot = '',

    [Parameter(ParameterSetName = 'Deploy')]
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [Parameter(ParameterSetName = 'Deploy')]
    [ValidateSet('x64', 'arm64')]
    [string]$Platform = 'x64',

    [Parameter(ParameterSetName = 'Deploy')]
    [switch]$SkipArtifacts,

    [Parameter(Mandatory = $true, ParameterSetName = 'Invoke')]
    [string]$ScriptPath,

    [Parameter(ParameterSetName = 'Invoke')]
    [hashtable]$ArgumentList = @{}
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

function Enable-XdpRemoting {
    if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Error "Run this from an elevated PowerShell on the TEST machine."
    }

    Write-Host "Enabling PowerShell remoting (WinRM) ..."
    Enable-PSRemoting -Force -SkipNetworkProfileCheck | Out-Null

    # Allow long-running test invocations and large payload pushes.
    Write-Host "Tuning WinRM limits for large file transfer and long tests ..."
    Set-Item WSMan:\localhost\MaxEnvelopeSizekb 8192 -Force
    Set-Item WSMan:\localhost\Shell\MaxMemoryPerShellMB 4096 -Force
    Set-Item WSMan:\localhost\Shell\IdleTimeout 7200000 -Force
    Set-Item WSMan:\localhost\Service\Auth\Negotiate $true -Force

    $hostName = [System.Net.Dns]::GetHostName()
    Write-Host ""
    Write-Host "Test machine is ready for remoting." -ForegroundColor Green
    Write-Host "From the dev machine, just run e.g. '.\tools\functional.ps1 -ComputerName $hostName'."
    Write-Host "For workgroup or IP-based connections, the dev script will automatically"
    Write-Host "add the test machine to WSMan TrustedHosts on first use (elevation required)."
}

# Ensure $ComputerName is in the local WSMan TrustedHosts list when needed.
# Domain-joined Kerberos targets do not require this; for workgroup / IP
# targets WinRM rejects the connection until the host is trusted. We add it
# automatically so the dev-side commands are copy/paste friendly.
function Add-XdpTrustedHostIfNeeded {
    param ([Parameter(Mandatory = $true)] [string]$ComputerName)

    function script:Test-XdpTrustedHost {
        param ([string]$Name)
        try {
            $value = (Get-Item WSMan:\localhost\Client\TrustedHosts -ErrorAction Stop).Value
        } catch {
            return $false
        }
        if ([string]::IsNullOrWhiteSpace($value)) { return $false }
        $entries = $value.Split(',') | ForEach-Object { $_.Trim() }
        return ($entries -contains '*' -or $entries -contains $Name)
    }

    if (Test-XdpTrustedHost -Name $ComputerName) { return }

    if (Test-Admin) {
        Write-Host "Adding '$ComputerName' to WSMan TrustedHosts ..." -ForegroundColor Yellow
        Set-Item WSMan:\localhost\Client\TrustedHosts -Value $ComputerName -Concatenate -Force
        return
    }

    Write-Host "Adding '$ComputerName' to WSMan TrustedHosts (requires elevation) ..." -ForegroundColor Yellow

    # The elevated child writes its diagnostics to a log file, since
    # -RedirectStandardError is not allowed with -Verb RunAs.
    $tmpDir   = [System.IO.Path]::GetTempPath()
    $stamp    = [guid]::NewGuid().ToString('N')
    $logPath  = Join-Path $tmpDir ("xdp-trustedhosts-$stamp.log")
    $scriptPath = Join-Path $tmpDir ("xdp-trustedhosts-$stamp.ps1")

    $scriptBody = @"
`$ErrorActionPreference = 'Stop'
Start-Transcript -Path '$logPath' -Force | Out-Null
try {
    Start-Service WinRM -ErrorAction SilentlyContinue
    `$existing = ''
    try { `$existing = (Get-Item WSMan:\localhost\Client\TrustedHosts -ErrorAction Stop).Value } catch {}
    `$entries = @()
    if (-not [string]::IsNullOrWhiteSpace(`$existing)) {
        `$entries = `$existing.Split(',') | ForEach-Object { `$_.Trim() } | Where-Object { `$_ }
    }
    if (`$entries -notcontains '*' -and `$entries -notcontains '$ComputerName') {
        `$entries += '$ComputerName'
        `$new = (`$entries -join ',')
        try {
            Set-Item WSMan:\localhost\Client\TrustedHosts -Value `$new -Force
        } catch {
            Write-Output "Set-Item failed: `$_"
            cmd.exe /c "winrm set winrm/config/client `@`{TrustedHosts=`"`$new`"`}"
        }
    }
    Stop-Transcript | Out-Null
    exit 0
} catch {
    Write-Output `$_
    Stop-Transcript | Out-Null
    exit 1
}
"@
    Set-Content -Path $scriptPath -Value $scriptBody -Encoding UTF8

    try {
        $proc = Start-Process -FilePath 'powershell.exe' `
            -ArgumentList @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath) `
            -Verb RunAs -Wait -PassThru -ErrorAction Stop
        if ($proc.ExitCode -ne 0) {
            Write-Warning "Elevated TrustedHosts update exited with code $($proc.ExitCode)."
            if (Test-Path $logPath) {
                $logText = (Get-Content -Raw $logPath).Trim()
                if ($logText) { Write-Warning $logText }
            }
        }
    } catch {
        Write-Warning "Could not auto-elevate to update TrustedHosts: $_"
    } finally {
        Remove-Item $logPath, $scriptPath -Force -ErrorAction SilentlyContinue
    }

    if (-not (Test-XdpTrustedHost -Name $ComputerName)) {
        Write-Error @"
WSMan TrustedHosts still does not include '$ComputerName'. WinRM will reject
the connection. Run once from an elevated PowerShell on this dev machine:

    Set-Item WSMan:\localhost\Client\TrustedHosts -Value '$ComputerName' -Concatenate -Force

Then retry your command.
"@
    }
}

function Connect-XdpRemote {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true)] [string]$ComputerName,
        [System.Management.Automation.PSCredential]$Credential
    )

    Add-XdpTrustedHostIfNeeded -ComputerName $ComputerName

    # Cache credentials per target host in the user's PowerShell session so
    # subsequent commands don't prompt repeatedly. The cache lives on the
    # global scope so it survives across script invocations within the same
    # process.
    $cacheVar = Get-Variable -Name XdpRemoteCredentialCache -Scope Global -ErrorAction SilentlyContinue
    if ($cacheVar) {
        $cache = $cacheVar.Value
    } else {
        $cache = @{}
        Set-Variable -Name XdpRemoteCredentialCache -Scope Global -Value $cache
    }
    if (-not $Credential -and $cache.ContainsKey($ComputerName)) {
        $Credential = $cache[$ComputerName]
    }

    $attempts = 0
    while ($true) {
        $attempts++
        $sessionParams = @{ ComputerName = $ComputerName; ErrorAction = 'Stop' }
        if ($Credential) { $sessionParams.Credential = $Credential }

        try {
            Write-Verbose "Opening PSSession to $ComputerName ..."
            $session = New-PSSession @sessionParams
            if ($Credential) {
                $cache[$ComputerName] = $Credential
            }
            return $session
        } catch {
            $msg = "$_"
            $isAuth = $msg -match 'Access is denied' -or $msg -match 'authentication' -or
                      $msg -match 'logon failure' -or $msg -match 'user name or password'
            if (-not $isAuth -or $attempts -ge 2) {
                throw
            }
            Write-Host "Authentication required for $ComputerName." -ForegroundColor Yellow
            $defaultUser = "$ComputerName\Administrator"
            $userName = Read-Host "Username [$defaultUser]"
            if ([string]::IsNullOrWhiteSpace($userName)) { $userName = $defaultUser }
            $password = Read-Host "Password for $userName" -AsSecureString
            if (-not $password -or $password.Length -eq 0) {
                throw
            }
            $Credential = New-Object System.Management.Automation.PSCredential($userName, $password)
        }
    }
}

function Get-XdpDeployManifest {
    param (
        [Parameter(Mandatory = $true)] [string]$Config,
        [Parameter(Mandatory = $true)] [string]$Platform,
        [switch]$SkipArtifacts
    )

    $items = @(
        @{ Path = "tools";         Required = $true }
    )

    if (-not $SkipArtifacts) {
        $items += @{
            Path     = (Get-ArtifactBinPathBase -Config $Config -Platform $Platform)
            Required = $true
        }
    }

    return $items
}

function Deploy-XdpRemote {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true)] [string]$ComputerName,
        [System.Management.Automation.PSCredential]$Credential,
        [string]$RemoteRoot,
        [Parameter(Mandatory = $true)] [string]$Config,
        [Parameter(Mandatory = $true)] [string]$Platform,
        [switch]$SkipArtifacts
    )

    if ([string]::IsNullOrEmpty($RemoteRoot)) { $RemoteRoot = Get-XdpDefaultRemoteRoot }

    $manifest = Get-XdpDeployManifest -Config $Config -Platform $Platform -SkipArtifacts:$SkipArtifacts

    $session = Connect-XdpRemote -ComputerName $ComputerName -Credential $Credential
    try {
        Invoke-Command -Session $session -ScriptBlock {
            param($Root)
            if (-not (Test-Path $Root)) { New-Item -ItemType Directory -Path $Root -Force | Out-Null }
        } -ArgumentList $RemoteRoot

        # Clean the remote artifacts\bin\<plat>_<cfg>\ directory before
        # copying. Stale files (e.g. from a prior build of a different
        # configuration) cause Copy-Item conflicts and confusing test errors.
        if (-not $SkipArtifacts) {
            $remoteBin = Join-Path $RemoteRoot (Get-ArtifactBinPathBase -Config $Config -Platform $Platform)
            Write-Host "Cleaning $ComputerName`:$remoteBin ..."
            Invoke-Command -Session $session -ScriptBlock {
                param($Path)
                if (Test-Path $Path) { Remove-Item $Path -Recurse -Force -ErrorAction Stop }
            } -ArgumentList $remoteBin
        }

        Write-Host "Copying files to $ComputerName`:$RemoteRoot ..."
        foreach ($item in $manifest) {
            $sources = @(Get-Item -Path (Join-Path $RootDir $item.Path) -ErrorAction SilentlyContinue)
            if ($sources.Count -eq 0) {
                if ($item.Required) {
                    Write-Error "Required path not found: $($item.Path). Did you build $Config\$Platform?"
                }
                continue
            }
            foreach ($src in $sources) {
                $relative = $src.FullName.Substring($RootDir.Length).TrimStart('\','/')
                $dest = Join-Path $RemoteRoot $relative
                $destParent = Split-Path $dest -Parent

                Invoke-Command -Session $session -ScriptBlock {
                    param($Parent, $Target, $IsContainer)
                    if (-not (Test-Path $Parent)) { New-Item -ItemType Directory -Path $Parent -Force | Out-Null }
                    if ($IsContainer -and (Test-Path $Target)) { Remove-Item $Target -Recurse -Force }
                } -ArgumentList $destParent, $dest, $src.PSIsContainer

                Write-Host "  -> $relative"
                if ($src.PSIsContainer) {
                    Copy-Item -ToSession $session -Path $src.FullName -Destination $dest -Recurse -Force
                } else {
                    Copy-Item -ToSession $session -Path $src.FullName -Destination $dest -Force
                }
            }
        }

        Write-Host "Deployment complete: $ComputerName -> $RemoteRoot" -ForegroundColor Green
    } finally {
        Remove-PSSession $session
    }
}

function Invoke-XdpRemote {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true)] [string]$ComputerName,
        [System.Management.Automation.PSCredential]$Credential,
        [string]$RemoteRoot,
        [Parameter(Mandatory = $true)] [string]$ScriptPath,
        [hashtable]$ArgumentList = @{}
    )

    if ([string]::IsNullOrEmpty($RemoteRoot)) { $RemoteRoot = Get-XdpDefaultRemoteRoot }

    # Normalize: accept absolute paths under $RootDir or repo-relative paths.
    if ([System.IO.Path]::IsPathRooted($ScriptPath)) {
        if (-not $ScriptPath.StartsWith($RootDir, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Error "ScriptPath '$ScriptPath' is not under the repo root '$RootDir'."
        }
        $ScriptPath = $ScriptPath.Substring($RootDir.Length).TrimStart('\','/')
    }
    $ScriptPath = $ScriptPath -replace '/', '\'

    $session = Connect-XdpRemote -ComputerName $ComputerName -Credential $Credential
    try {
        Write-Host "Running $ScriptPath on $ComputerName ..." -ForegroundColor Cyan

        # Stream remote output to the local host as it arrives. The remote
        # side merges all streams (*>&1) and converts them to plain strings
        # via Out-String -Stream so deserialized error/warning records
        # don't get re-emitted as red errors locally. Piping
        # Invoke-Command's output directly into Write-Host preserves the
        # live streaming behavior PowerShell remoting provides natively.
        # The exit code is emitted as a final sentinel line so we don't
        # need a follow-up call against a runspace that may have already
        # torn down.
        $exitCode = 0
        $sentinel = '##XDP-REMOTE-EXIT##'
        Invoke-Command -Session $session -ScriptBlock {
            param($Root, $Rel, $ScriptArgs, $Sentinel)
            $ErrorActionPreference = 'Continue'
            Set-Location $Root
            try {
                & (Join-Path $Root $Rel) @ScriptArgs *>&1 | Out-String -Stream
                $code = $LASTEXITCODE
            } catch {
                # Surface the error as text so it streams cleanly, then
                # mark a non-zero exit code.
                ($_ | Out-String).TrimEnd()
                $code = 1
            }
            "$Sentinel $code"
        } -ArgumentList $RemoteRoot, $ScriptPath, $ArgumentList, $sentinel | ForEach-Object {
            if ($_ -is [string] -and $_.StartsWith($sentinel)) {
                $val = $_.Substring($sentinel.Length).Trim()
                if (-not [string]::IsNullOrEmpty($val)) {
                    [int]::TryParse($val, [ref]$exitCode) | Out-Null
                }
            } else {
                Write-Host $_
            }
        }

        if ($exitCode -ne 0) {
            $global:LASTEXITCODE = $exitCode
            Write-Error "Remote script exited with code $exitCode"
        }
    } finally {
        Remove-PSSession $session
    }
}

# When this script is dot-sourced (no parameter set selected), just expose
# the helper functions and return.
if ($PSCmdlet.ParameterSetName -eq 'None') {
    return
}

switch ($PSCmdlet.ParameterSetName) {
    'EnableRemoting' { Enable-XdpRemoting }
    'Deploy'         {
        Deploy-XdpRemote -ComputerName $ComputerName -Credential $Credential `
            -RemoteRoot $RemoteRoot -Config $Config -Platform $Platform -SkipArtifacts:$SkipArtifacts
    }
    'Invoke'         {
        Invoke-XdpRemote -ComputerName $ComputerName -Credential $Credential `
            -RemoteRoot $RemoteRoot -ScriptPath $ScriptPath -ArgumentList $ArgumentList
    }
}
