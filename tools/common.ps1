#
# Helper functions for XDP project.
#

# Disable Invoke-WebRequest progress bar to work around a bug that slows downloads.
$ProgressPreference = 'SilentlyContinue'

function Invoke-WebRequest-WithRetry {
    param (
        [Parameter()]
        [string]$Uri,

        [Parameter()]
        [string]$OutFile
    )

    $MaxTries = 10
    $Success = $false

    foreach ($i in 1..$MaxTries) {
        try {
            Invoke-WebRequest -Uri $Uri -OutFile $OutFile
            $Success = $true
            break;
        } catch [System.Net.WebException] {
            if ($i -lt $MaxTries) {
                Write-Verbose "Invoke-WebRequest-WithRetry [$i/$MaxTries] Failed to download $Uri"
                Start-Sleep -Seconds $i
            } else {
                throw
            }
        }
    }
}

function Get-CurrentBranch {
    $env:GIT_REDIRECT_STDERR = '2>&1'
    $CurrentBranch = git branch --show-current
    if ([string]::IsNullOrWhiteSpace($CurrentBranch)) {
        Write-Warning "Failed to get branch from git"
        return $null
    }
    return $CurrentBranch
}

# Returns the target or current git branch.
function Get-BuildBranch {
    if (![string]::IsNullOrWhiteSpace($env:SYSTEM_PULLREQUEST_TARGETBRANCH)) {
        # We are in a (AZP) pull request build.
        Write-Host "Using SYSTEM_PULLREQUEST_TARGETBRANCH=$env:SYSTEM_PULLREQUEST_TARGETBRANCH to compute branch"
        return $env:SYSTEM_PULLREQUEST_TARGETBRANCH

    } elseif (![string]::IsNullOrWhiteSpace($env:GITHUB_BASE_REF)) {
        # We are in a (GitHub Action) pull request build.
        Write-Host "Using GITHUB_BASE_REF=$env:GITHUB_BASE_REF to compute branch"
        return $env:GITHUB_BASE_REF

    } elseif (![string]::IsNullOrWhiteSpace($env:BUILD_SOURCEBRANCH)) {
        # We are in a (AZP) main build.
        Write-Host "Using BUILD_SOURCEBRANCH=$env:BUILD_SOURCEBRANCH to compute branch"
        $env:BUILD_SOURCEBRANCH -match 'refs/(?:heads/)?(.+)' | Out-Null
        return $Matches[1]

    } elseif (![string]::IsNullOrWhiteSpace($env:GITHUB_REF_NAME)) {
        # We are in a (GitHub Action) main build.
        Write-Host "Using GITHUB_REF_NAME=$env:GITHUB_REF_NAME to compute branch"
        return $env:GITHUB_REF_NAME
        $CommitMergedData = $true

    } else {
        # Fallback to the current branch.
        return Get-CurrentBranch
    }
}

function Is-ReleaseBuild {
    return ((Get-BuildBranch) -match '^tags/v\d+\.\d+\.\d+$')
}

function Get-VsTestPath {
    # Unfortunately CI doesn't add vstest to PATH. Test existence of vstest
    # install paths instead.

    $ManualVsTestPath = "$(Split-Path $PSScriptRoot -Parent)\artifacts\Microsoft.TestPlatform\tools\net462\Common7\IDE\Extensions\TestPlatform"
    if (Test-Path $ManualVsTestPath) {
        return $ManualVsTestPath
    }

    $CiVsTestPath = "${Env:ProgramFiles(X86)}\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\Extensions\TestPlatform"
    if (Test-Path $CiVsTestPath) {
        return $CiVsTestPath
    }

    return $null
}

# Returns the XDP installation path
function Get-XdpInstallPath {
    return "$($env:SystemDrive)\xdpruntime"
}

# Returns the eBPF installation path
function Get-EbpfInstallPath {
    return "$($env:SystemDrive)\ebpf"
}

function Get-EbpfVersion {
    return "0.21.0"
}

# Returns the eBPF MSI full path
function Get-EbpfMsiFullPath {
    param (
        [Parameter()]
        [string]$Platform
    )
    $RootDir = Split-Path $PSScriptRoot -Parent
    $EbpfVersion = Get-EbpfVersion
    return "$RootDir\artifacts\ebpfmsi\ebpf-for-windows.$EbpfVersion.$Platform.msi"
}

function Get-EbpfMsiUrl {
    param (
        [Parameter()]
        [string]$Platform
    )
    $EbpfVersion = Get-EbpfVersion
    return "https://github.com/microsoft/ebpf-for-windows/releases/download/Release-v$EbpfVersion/ebpf-for-windows.$Platform.$EbpfVersion.msi"
}

function Get-FnVersion {
    return "1.3.0"
}

function Get-FnRuntimeUrl {
    param (
        [Parameter()]
        [string]$Platform
    )
    "https://github.com/microsoft/win-net-test/releases/download/v$(Get-FnVersion)/fn-runtime-$Platform.zip"
}

function Get-FnRuntimeDir {
    $RootDir = Split-Path $PSScriptRoot -Parent
    return "$RootDir/artifacts/fn/runtime-$(Get-FnVersion)"
}

function Get-CoreNetCiCommit {
    return "285f2f66ac3319220c663312e93da28af9e9365e"
}

function Get-CoreNetCiArtifactPath {
    param (
        [Parameter()]
        [string]$Name
    )

    $RootDir = Split-Path $PSScriptRoot -Parent
    $Commit = Get-CoreNetCiCommit

    return "$RootDir\artifacts\corenet-ci-$Commit\vm-setup\$Name"
}

function Get-ArtifactBinPathBase {
    param (
        [Parameter()]
        [string]$Config,
        [Parameter()]
        [string]$Platform
    )

    return "artifacts\bin\$($Platform)_$($Config)"
}

function Get-ArtifactBinPath {
    param (
        [Parameter()]
        [string]$Config,
        [Parameter()]
        [string]$Platform
    )

    $RootDir = Split-Path $PSScriptRoot -Parent
    return "$RootDir\$(Get-ArtifactBinPathBase -Config $Config -Platform $Platform)"
}

function Get-XdpBuildVersion {
    $RootDir = Split-Path $PSScriptRoot -Parent
    $XdpBuildVersion = @{}
    [xml]$XdpVersion = Get-Content $RootDir\src\xdp.props
    $XdpVersionPropertyGroup = $XdpVersion.Project.PropertyGroup |
        Where-Object {$_.PSObject.Properties.Name -contains "Label" -and $_.Label -eq "Version"}
    $XdpBuildVersion.Major = $XdpVersionPropertyGroup.XdpMajorVersion
    $XdpBuildVersion.Minor = $XdpVersionPropertyGroup.XdpMinorVersion
    $XdpBuildVersion.Patch = $XdpVersionPropertyGroup.XdpPatchVersion
    return $XdpBuildVersion;
}

function Get-XdpBuildVersionString {
    $XdpVersion = Get-XdpBuildVersion
    $VersionString = "$($XdpVersion.Major).$($XdpVersion.Minor).$($XdpVersion.Patch)"

    if (!(Is-ReleaseBuild)) {
        $VersionString += "-prerelease-" + (git.exe describe --long --always --dirty --exclude=* --abbrev=8)
    }

    return $VersionString;
}

function Get-OsBuildVersionString {
    return (Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').BuildLabEx
}

# Returns whether the script is running as a built-in administrator.
function Test-Admin {
    return ([Security.Principal.WindowsPrincipal] `
        [Security.Principal.WindowsIdentity]::GetCurrent() `
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Refreshes the PATH environment variable.
function Refresh-Path {
    $env:Path=(
        [System.Environment]::GetEnvironmentVariable("Path","Machine"),
        [System.Environment]::GetEnvironmentVariable("Path","User")
    ) -match '.' -join ';'
}

function Add-Path {
    param (
        [Parameter()]
        [string]$NewPath
    )
    [Environment]::SetEnvironmentVariable("Path", $env:Path + ";$NewPath", "Machine")
    Refresh-Path
}

function Collect-LiveKD {
    param (
        [Parameter()]
        [string]$OutFile
    )

    $LiveKD = Get-CoreNetCiArtifactPath -Name "livekd64.exe"
    $KD = Get-CoreNetCiArtifactPath -Name "kd.exe"

    Write-Host "Capturing live kernel dump to $OutFile"

    New-Item -ItemType Directory -Force -Path (Split-Path $OutFile) | Out-Null
    Write-Verbose "$LiveKD -o $OutFile -k $KD -ml -accepteula"
    & $LiveKD -o $OutFile -k $KD -ml -accepteula
}

function Collect-ProcessDump {
    param (
        [Parameter()]
        [string]$ProcessName,

        [Parameter()]
        [string]$OutFile,

        [Parameter()]
        [int]$ProcessId = 0
    )

    $ProcDump = Get-CoreNetCiArtifactPath -Name "procdump64.exe"


    New-Item -ItemType Directory -Force -Path (Split-Path $OutFile) | Out-Null

    if ($ProcessId -ne 0) {
        $ProcessArg = $ProcessId
    } else {
        $ProcessArg = $ProcessName
    }

    Write-Host "Capturing process dump of $ProcessName to $OutFile"

    Write-Verbose "$ProcDump -accepteula -ma $ProcessArg $OutFile"
    & $ProcDump -accepteula -ma $ProcessArg $OutFile
}

function Initiate-Bugcheck {
    param (
        [Parameter()]
        [string]$Code = "0xE2"
    )

    $NotMyFault = Get-CoreNetCiArtifactPath -Name "notmyfault64.exe"

    Write-Host "$NotMyFault -accepteula -bugcheck $Code"
    & $NotMyFault -accepteula -bugcheck $Code
}

function New-PerfDataSet {
    param (
        [Parameter()]
        [string[]]$Files = @()
    )

    $Results = [System.Collections.ArrayList]::new()

    foreach ($File in $Files) {
        if (-not [string]::IsNullOrEmpty($File) -and (Test-Path $File)) {
            $Results.AddRange($(Get-Content -Raw $File | ConvertFrom-Json))
        }
    }

    return Write-Output -NoEnumerate $Results
}

function Write-PerfDataSet {
    param (
        [Parameter()]
        [object]$DataSet,

        [Parameter()]
        [string]$File
    )

    New-Item -ItemType Directory -Force -Path (Split-Path $File) | Out-Null
    ConvertTo-Json -InputObject $DataSet -Depth 100 | Out-File -FilePath $File -Encoding utf8
}

function New-PerfData {
    param (
        [Parameter()]
        [string]$ScenarioName,

        [Parameter()]
        [string]$Platform,

        [Parameter()]
        [string]$CommitHash,

        [Parameter()]
        [hashtable[]]$Metrics
    )

    return @{
        ScenarioName = $ScenarioName
        Platform = $Platform
        CommitHash = $CommitHash
        Timestamp = (Get-Date).toString("o")
        OsBuild = Get-OsBuildVersionString
        MachineName = $env:ComputerName
        Metrics = $Metrics
    }
}
