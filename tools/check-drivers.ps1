<#

.SYNOPSIS
This checks for the presence of any XDP drivers currently loaded.

#>

[cmdletbinding()]Param(
    [ValidateSet("Debug", "Release")]
    [Parameter(Mandatory=$false)]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Platform = "x64",

    # Allow eBPF drivers to be and remain present
    [Parameter(Mandatory = $false)]
    [switch]$AllowEbpf = $false,

    # Pass -Force to the underlying setup.ps1 so that normally-fatal
    # uninstall errors are treated as recoverable warnings. Use after a
    # bugcheck has corrupted on-disk or registry state.
    [Parameter(Mandatory = $false)]
    [switch]$Force = $false,

    [Parameter(Mandatory = $false)]
    [string]$ComputerName = "",

    [Parameter(Mandatory = $false)]
    [System.Management.Automation.PSCredential]$Credential,

    [Parameter(Mandatory = $false)]
    [string]$RemoteRoot = "",

    [Parameter(Mandatory = $false)]
    [switch]$SkipDeploy
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

# Important paths.
$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

$Forwarded = Invoke-XdpRemoteIfRequested -InvocationCommand $MyInvocation.MyCommand `
    -BoundParameters $PSBoundParameters -Config $Config -Platform $Platform
if ($Forwarded -is [array]) { $Forwarded = $Forwarded[-1] }
if ($Forwarded) { return }

# Detects files written to disk in the seconds before a bugcheck and left
# truncated / NUL-filled. The XDP runtime layout under C:\xdpruntime is the
# most common victim because tools\setup.ps1 expands a nupkg there during
# install. If any of those files look corrupt, blow away the directory so a
# subsequent install can recreate it cleanly.
function Repair-CorruptedXdpRuntime {
    $XdpRuntime = Get-XdpInstallPath

    $needsReExtract = $false

    if (-not (Test-Path $XdpRuntime)) {
        # The runtime dir has been removed (possibly by a previous repair
        # attempt) but xdp.sys is still loaded - we still need the
        # nupkg-based uninstall script to drive teardown, so re-extract.
        if (Get-Service xdp -ErrorAction SilentlyContinue) {
            Write-Host "$XdpRuntime is missing but xdp service is still installed." -ForegroundColor Yellow
            $needsReExtract = $true
        } else {
            return
        }
    } else {
        $files = Get-ChildItem -Path $XdpRuntime -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -gt 0 -and $_.Length -lt 8MB }
        foreach ($f in $files) {
            try {
                $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
            } catch { continue }
            if ($bytes.Length -eq 0) { continue }
            # A file that is entirely zero is unambiguous post-bugcheck damage.
            $allZero = $true
            for ($i = 0; $i -lt $bytes.Length; $i++) {
                if ($bytes[$i] -ne 0) { $allZero = $false; break }
            }
            if ($allZero) {
                Write-Host "Detected NUL-filled file from a prior bugcheck: $($f.FullName)" -ForegroundColor Yellow
                $needsReExtract = $true
                break
            }
        }
    }

    if (-not $needsReExtract) { return }

    if (Test-Path $XdpRuntime) {
        Write-Host "Removing $XdpRuntime so it can be re-extracted from the nupkg." -ForegroundColor Yellow
        try {
            Remove-Item $XdpRuntime -Recurse -Force -ErrorAction Stop
        } catch {
            Write-Warning "Could not remove $XdpRuntime`: $_"
            return
        }
    }

    # Re-extract the runtime nupkg so the regular uninstall path
    # (which delegates to xdpruntime\xdp-setup.ps1) has a working
    # script to invoke.
    $ArtifactsBin = Get-ArtifactBinPath -Config $Config -Platform $Platform
    $Nupkg = Get-ChildItem -Path "$ArtifactsBin\packages" `
        -Filter "Microsoft.XDP-for-Windows.Runtime.$Platform.*.nupkg" `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $Nupkg) {
        Write-Warning "No runtime nupkg found under $ArtifactsBin\packages; cannot re-extract."
        return
    }
    Write-Host "Re-extracting $($Nupkg.Name) into $XdpRuntime ..." -ForegroundColor Yellow
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($Nupkg.FullName, $XdpRuntime)
}

if ($Force) {
    Repair-CorruptedXdpRuntime
}

# Cache driverquery output.
$AllDrivers = driverquery /v /fo list

# Checks for the presence of a loaded driver.
function Check-Driver($Driver) {
    $Found = $false
    try {
        $Found = ($AllDrivers | Select-String $Driver).Matches[0].Success
    } catch { }
    $Found
}

# Checks for the presence of a loaded driver and attempts to uninstall it.
function Check-And-Remove-Driver($Driver, $Component) {
    if (Check-Driver $Driver) {
        Write-Host "Detected $Driver is loaded. Uninstalling $Component..."
        & $RootDir\tools\setup.ps1 -Uninstall $Component -Config $Config -Platform $Platform -Force:$Force

        # Update cached driverquery output.
        $script:AllDrivers = driverquery /v /fo list
    }

    if ($Force) {
        # Components installed via raw `sc.exe create` (fndis, xskfwdkm) leave
        # behind a service registration after surprise reboots. The Check-Driver
        # path only sees currently-loaded drivers.
        $svc = Get-Service -Name $Component -ErrorAction SilentlyContinue
        if ($svc) {
            Write-Host "Removing residual $Component service registration..." -ForegroundColor Yellow
            if ($svc.Status -ne 'Stopped') {
                try { Stop-Service -Name $Component -Force -ErrorAction Stop } catch { }
            }
            sc.exe delete $Component | Write-Verbose
        }
    }

    if (Check-Driver $Driver) {
        $AllDrivers | Write-Verbose
        Write-Error "$Driver loaded!"
    }
}

# Check for any XDP drivers.
Check-And-Remove-Driver "xskfwdkm.sys" "xskfwdkm"
Check-And-Remove-Driver "fnmp.sys" "fnmp"
Check-And-Remove-Driver "fnlwf.sys" "fnlwf"
Check-And-Remove-Driver "xdpmp.sys" "xdpmp"
Check-And-Remove-Driver "xdp.sys" "xdp"
Check-And-Remove-Driver "fndis.sys" "fndis"

# Check for any eBPF drivers.
#
# Some CI images ship eBPF inbox. Tests must leave any inbox eBPF exactly as
# they found it. The caller drives this: the test prologue passes -AllowEbpf
# (tolerate eBPF and record whether it is present as a pipeline baseline), and
# the test epilogue passes -AllowEbpf based on that recorded baseline -
# tolerating eBPF when it was present originally, or removing it to restore the
# original state when it was not.
$EbpfDrivers = @("ebpfcore.sys", "netebpfext.sys")

if ($AllowEbpf) {
    $DetectedEbpfDrivers = @($EbpfDrivers | Where-Object { Check-Driver $_ })
    $EbpfPresent = $DetectedEbpfDrivers.Count -gt 0
    if ($EbpfPresent) {
        Write-Verbose "Detected allowed eBPF driver(s), not removing: $($DetectedEbpfDrivers -join ', ')"
    }
    if ($env:GITHUB_ENV) {
        Write-Verbose "Setting GITHUB_ENV XDP_EBPF_BASELINE=$([int]$EbpfPresent)"
        Add-Content -Path $env:GITHUB_ENV -Value "XDP_EBPF_BASELINE=$([int]$EbpfPresent)"
    }
} else {
    foreach ($EbpfDriver in $EbpfDrivers) {
        Check-And-Remove-Driver $EbpfDriver "ebpf"
    }
}

# Yay! No XDP drivers found.
Write-Host "No loaded XDP drivers found!"

# Log driver verifier status
verifier.exe /query
