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
        # We are in a (GitHub Action) main build. When the run is on a tag ref
        # (e.g. ci.yml dispatched via "workflow_dispatch --ref refs/tags/vX.Y.Z"),
        # GitHub natively reports GITHUB_REF_TYPE=tag, so surface it as a tag so
        # Get-BuildTag treats it as a release build.
        Write-Host "Using GITHUB_REF_NAME=$env:GITHUB_REF_NAME (GITHUB_REF_TYPE=$env:GITHUB_REF_TYPE) to compute branch"
        if ($env:GITHUB_REF_TYPE -eq 'tag') {
            return "tags/$env:GITHUB_REF_NAME"
        }
        return $env:GITHUB_REF_NAME

    } else {
        # Fallback to the current branch.
        return Get-CurrentBranch
    }
}

function Get-BuildTag {
    if (((Get-BuildBranch) -match '^tags/v(\d+\.\d+\.\d+.*)$')) {
        return $Matches[1]
    }

    return $null
}

function Is-ReleaseBuild {
    return Get-BuildTag -ne $null
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
    return "1.3.0"
}

# Returns the GitHub release tag for a given eBPF for Windows version. Releases
# up to and including v1.1.0 use the "Release-v" tag prefix; newer releases use
# a bare "v" prefix.
function Get-EbpfReleaseTag {
    param (
        [Parameter(Mandatory = $true)]
        [string]$Version
    )
    if ([version]$Version -le [version]"1.1.0") {
        return "Release-v$Version"
    }
    return "v$Version"
}

# Returns the eBPF MSI full path
function Get-EbpfMsiFullPath {
    param (
        [Parameter()]
        [string]$Platform,

        [Parameter()]
        [string]$Version = (Get-EbpfVersion)
    )
    $RootDir = Split-Path $PSScriptRoot -Parent
    return "$RootDir\artifacts\ebpfmsi\ebpf-for-windows.$Version.$Platform.msi"
}

function Get-EbpfMsiUrl {
    param (
        [Parameter()]
        [string]$Platform,

        [Parameter()]
        [string]$Version = (Get-EbpfVersion)
    )
    $Tag = Get-EbpfReleaseTag -Version $Version
    return "https://github.com/microsoft/ebpf-for-windows/releases/download/$Tag/ebpf-for-windows.$Platform.$Version.msi"
}

# Returns $true if eBPF appears to be an inbox (OS-provided) component rather
# than installed via the eBPF-for-Windows MSI package.
function Test-EbpfInbox {
    $svcKey = Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\ebpfcore" -ErrorAction SilentlyContinue
    if ($null -eq $svcKey) { return $false }
    $imagePath = if ($svcKey.ImagePath) { $svcKey.ImagePath } else { "" }
    $ebpfInstallPath = Get-EbpfInstallPath
    # Inbox drivers have a system image path (e.g. \SystemRoot\System32\drivers\)
    # and no MSI install directory. Exclude stale MSI state where the service
    # still references the MSI install path.
    return ($imagePath -notlike "$ebpfInstallPath*") -and !(Test-Path $ebpfInstallPath)
}

function Get-FnVersion {
    return "1.5.0"
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

# If -ComputerName was passed to a top-level script, forward execution to the
# remote test machine via tools\remote.ps1 and exit. Returns $true when the
# command was forwarded (caller should exit), $false otherwise.
function Get-XdpDefaultRemoteRoot {
    return 'C:\xdp'
}

function Invoke-XdpRemoteIfRequested {
    [CmdletBinding()]
    param (
        # The MyInvocation.MyCommand of the caller; used to compute repo-relative path.
        [Parameter(Mandatory = $true)] $InvocationCommand,
        # The PSBoundParameters of the caller.
        [Parameter(Mandatory = $true)] $BoundParameters,
        # Whether the caller's workflow needs the artifacts\bin tree on the
        # remote. Scripts that only operate on machine state (e.g.
        # prepare-machine.ps1, check-drivers.ps1) should pass $false to
        # avoid pushing large binaries unnecessarily.
        [bool]$DeployArtifacts = $true,
        # Build config/platform for the deploy step.
        [string]$Config = '',
        [string]$Platform = ''
    )

    $ComputerName = $null
    if ($BoundParameters.ContainsKey('ComputerName')) {
        $ComputerName = [string]$BoundParameters['ComputerName']
    }
    if ([string]::IsNullOrEmpty($ComputerName)) {
        # Fall back to a session-scoped default set via
        # Set-XdpRemoteDefault. Lets developers stop retyping
        # -ComputerName on every command.
        $defaultVar = Get-Variable -Name XdpRemoteDefault -Scope Global -ErrorAction SilentlyContinue
        if ($defaultVar -and -not [string]::IsNullOrEmpty([string]$defaultVar.Value)) {
            $ComputerName = [string]$defaultVar.Value
            Write-Host "Using session-default remote computer: $ComputerName" -ForegroundColor DarkGray
        }
    }
    if ([string]::IsNullOrEmpty($ComputerName)) {
        return $false
    }

    $Credential = $null
    if ($BoundParameters.ContainsKey('Credential')) { $Credential = $BoundParameters['Credential'] }
    $RemoteRoot = Get-XdpDefaultRemoteRoot
    if ($BoundParameters.ContainsKey('RemoteRoot') -and -not [string]::IsNullOrEmpty([string]$BoundParameters['RemoteRoot'])) {
        $RemoteRoot = [string]$BoundParameters['RemoteRoot']
    }
    $SkipDeploy = $false
    if ($BoundParameters.ContainsKey('SkipDeploy')) { $SkipDeploy = [bool]$BoundParameters['SkipDeploy'] }

    # Strip parameters that are meaningful only on the dev machine before
    # forwarding to the remote.
    $remoteArgs = @{}
    foreach ($key in @($BoundParameters.Keys)) {
        if ($key -in @('ComputerName','Credential','RemoteRoot','SkipDeploy')) { continue }
        $remoteArgs[$key] = $BoundParameters[$key]
    }

    $RootDir = Split-Path $PSScriptRoot -Parent
    $RemoteScript = $InvocationCommand.Path
    if ([string]::IsNullOrEmpty($RemoteScript)) {
        Write-Error "Unable to determine caller script path for remote forwarding."
    }
    $RelScript = $RemoteScript.Substring($RootDir.Length).TrimStart('\','/')

    $RemoteScriptPs1 = "$RootDir\tools\remote.ps1"

    if (-not $SkipDeploy) {
        $deployArgs = @{
            ComputerName = $ComputerName
            RemoteRoot   = $RemoteRoot
        }
        if (-not [string]::IsNullOrEmpty($Config))   { $deployArgs.Config   = $Config }
        if (-not [string]::IsNullOrEmpty($Platform)) { $deployArgs.Platform = $Platform }
        if (-not $DeployArtifacts) { $deployArgs.SkipArtifacts = $true }
        if ($Credential) { $deployArgs.Credential = $Credential }
        & $RemoteScriptPs1 -Deploy @deployArgs
    }

    $invokeArgs = @{
        ComputerName = $ComputerName
        RemoteRoot   = $RemoteRoot
        ScriptPath   = $RelScript
        ArgumentList = $remoteArgs
    }
    if ($Credential) { $invokeArgs.Credential = $Credential }
    & $RemoteScriptPs1 -Invoke @invokeArgs

    return $true
}

function Get-XdpBuildVersionString {
    param (
        [string]$ExtraMoniker = ""
    )
    $XdpVersion = Get-XdpBuildVersion
    $VersionString = "$($XdpVersion.Major).$($XdpVersion.Minor).$($XdpVersion.Patch)"

    if (Is-ReleaseBuild) {
        $TagVersion = Get-BuildTag
        if (!($TagVersion.StartsWith($VersionString))) {
            Write-Error "Tag version $TagVersion mismatches expected version $VersionString"
        }
        $VersionString = $TagVersion
    } else {
        $VersionString += "-prerelease-"
        if (-not [string]::IsNullOrEmpty($ExtraMoniker)) {
            $VersionString += "$ExtraMoniker-"
        }
        $VersionString += (git.exe describe --long --always --dirty --exclude=* --abbrev=8)
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

# Helper to start (with retry) a service.
function Start-Service-With-Retry($Name) {
    Write-Verbose "Start-Service $Name"
    $StartSuccess = $false
    for ($i=0; $i -lt 200; $i++) {
        try {
            Start-Sleep -Milliseconds 5
            Start-Service $Name
            $StartSuccess = $true
            break
        } catch { }
    }
    if ($StartSuccess -eq $false) {
        Write-Error "Failed to start $Name"
    }
}
