<#

.SYNOPSIS
This signs and packages the drivers.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$RootDir = Split-Path $PSScriptRoot -Parent
. $RootDir\tools\common.ps1

function Get-WindowsKitTool {
    param (
        [string]$Arch = "x86",
        [Parameter(Mandatory = $true)]
        [string]$Tool
    )

    $KitBinRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (!(Test-Path $KitBinRoot)) {
        Write-Error "Windows Kit Binary Folder not Found"
        return $null
    }


    $Subfolders = Get-ChildItem -Path $KitBinRoot -Directory | Sort-Object -Descending
    foreach ($Subfolder in $Subfolders) {
        $ToolPath = Join-Path $Subfolder.FullName "$Arch\$Tool"
        if (Test-Path $ToolPath) {
            return $ToolPath
        }
    }

    Write-Error "Failed to find tool"
    return $null
}

# Tool paths.
$SignToolPath = Get-WindowsKitTool -Tool "signtool.exe"
if (!(Test-Path $SignToolPath)) { Write-Error "$SignToolPath does not exist!" }
$Inf2CatToolPath = Get-WindowsKitTool -Tool "inf2cat.exe"
if (!(Test-Path $Inf2CatToolPath)) { Write-Error "$Inf2CatToolPath does not exist!" }

# Artifact paths.
$RootDir = (Split-Path $PSScriptRoot -Parent)
$ArtifactsDir = Join-Path $RootDir "artifacts\bin\$($Arch)_$($Config)"

# Certificate paths.
$CodeSignCertPath = Get-CoreNetCiArtifactPath -Name "CoreNetSignRoot.cer"
if (!(Test-Path $CodeSignCertPath)) { Write-Error "$CodeSignCertPath does not exist!" }
$CertPath = Get-CoreNetCiArtifactPath -Name "CoreNetSign.pfx"
if (!(Test-Path $CertPath)) { Write-Error "$CertPath does not exist!" }

# All the file paths.
$XdpDir = Join-Path $ArtifactsDir "xdp"
$XdpSys = Join-Path $XdpDir "xdp.sys"
$XdpInf = Join-Path $XdpDir "xdp.inf"
$XdpCat = Join-Path $XdpDir "xdp.cat"
$FndisSys = Join-Path $ArtifactsDir "fndis\fndis.sys"
$XdpMpDir = Join-Path $ArtifactsDir "xdpmp"
$XdpMpSys = Join-Path $XdpMpDir "xdpmp.sys"
$XdpMpInf = Join-Path $XdpMpDir "xdpmp.inf"
$XdpMpCat = Join-Path $XdpMpDir "xdpmp.cat"

# Verify all the files are present.
if (!(Test-Path $XdpSys)) { Write-Error "$XdpSys does not exist!" }
if (!(Test-Path $XdpInf)) { Write-Error "$XdpInf does not exist!" }
if (!(Test-Path $FndisSys)) { Write-Error "$FndisSys does not exist!" }
if (!(Test-Path $XdpMpSys)) { Write-Error "$XdpMpSys does not exist!" }
if (!(Test-Path $XdpMpInf)) { Write-Error "$XdpMpInf does not exist!" }

# Sign the driver files.
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 $XdpSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 $FndisSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 $XdpMpSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }

# Build up the catalogs.
& $Inf2CatToolPath /driver:$XdpDir /os:10_x64
if ($LastExitCode) { Write-Error "inf2cat.exe exit code: $LastExitCode" }
& $Inf2CatToolPath /driver:$XdpMpDir /os:10_x64
if ($LastExitCode) { Write-Error "inf2cat.exe exit code: $LastExitCode" }

# Sign the catalogs.
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 $XdpCat
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 $XdpMpCat
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }

# Copy the cert to the artifacts dir.
Copy-Item $CodeSignCertPath $ArtifactsDir
