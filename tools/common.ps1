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
    $CurrentBranch = $null
    try {
        $CurrentBranch = git branch --show-current
        if ([string]::IsNullOrWhiteSpace($CurrentBranch)) {
            throw
        }
    } catch {
        Write-Error "Failed to get branch from git"
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
        $env:BUILD_SOURCEBRANCH -match 'refs/heads/(.+)' | Out-Null
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

function Get-VsTestPath {
    # Unfortunately CI doesn't add vstest to PATH. Test existence of vstest
    # install paths instead.

    $ManualVsTestPath = "$(Split-Path $PSScriptRoot -Parent)\artifacts\Microsoft.TestPlatform\tools\net451\Common7\IDE\Extensions\TestPlatform"
    if (Test-Path $ManualVsTestPath) {
        return $ManualVsTestPath
    }

    $CiVsTestPath = "${Env:ProgramFiles(X86)}\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\Extensions\TestPlatform"
    if (Test-Path $CiVsTestPath) {
        return $CiVsTestPath
    }

    return $null
}
