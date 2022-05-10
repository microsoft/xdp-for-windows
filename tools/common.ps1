#
# Helper functions for XDP project.
#

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
    $PrBranchName = $env:SYSTEM_PULLREQUEST_TARGETBRANCH
    if ([string]::IsNullOrWhiteSpace($PrBranchName)) {
        # Mainline build, just get branch name
        $AzpBranchName = $env:BUILD_SOURCEBRANCH
        if ([string]::IsNullOrWhiteSpace($AzpBranchName)) {
            # Non-Azure build
            $BranchName = Get-CurrentBranch
        } else {
            # Azure Build
            $BuildReason = $env:BUILD_REASON
            if ("Manual" -eq $BuildReason) {
                $BranchName = "unknown"
            } else {
                $AzpBranchName -match 'refs/heads/(.+)' | Out-Null
                $BranchName = $Matches[1]
            }
        }
    } else {
        # PR Build
        $BranchName = $PrBranchName
    }
    return $BranchName
}
