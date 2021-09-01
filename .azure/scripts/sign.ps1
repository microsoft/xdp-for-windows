<#

.SYNOPSIS
This signs and packages the drivers.

.PARAMETER Config
    Specifies the build configuration to use.

.PARAMETER Arch
    The CPU architecture to use.

.PARAMETER CertPath
    The path to the certificate to use.

.PARAMETER GenerateCert
    Indicates whether to generate a certificate.

#>

param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x86", "x64", "arm", "arm64")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [string]$CertPath = "C:\CodeSign.pfx",

    [Parameter(Mandatory = $false)]
    [switch]$GenerateCert = $false
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

# Tool paths.
$ToolsDir = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x86"
$CertMgrPath = Join-Path $ToolsDir "certmgr.exe"
$SignToolPath = Join-Path $ToolsDir "signtool.exe"
$Inf2CatToolPath = Join-Path $ToolsDir "inf2cat.exe"

if (!(Test-Path $CertMgrPath)) {
    Write-Error "$CertMgrPath does not exist!"
}
if (!(Test-Path $SignToolPath)) {
    Write-Error "$SignToolPath does not exist!"
}
if (!(Test-Path $Inf2CatToolPath)) {
    Write-Error "$Inf2CatToolPath does not exist!"
}
if (!(Test-Path $CertPath)) {
    if ($GenerateCert) {
        # Generate the certificate.
        $PfxPassword = ConvertTo-SecureString -String "placeholder" -Force -AsPlainText
        $CodeSignCert = New-SelfSignedCertificate -Type Custom -Subject "CN=XdpTestCodeSignRoot" -FriendlyName XdpTestCodeSignRoot -KeyUsageProperty Sign -KeyUsage DigitalSignature -CertStoreLocation cert:\CurrentUser\My -HashAlgorithm SHA256 -Provider "Microsoft Software Key Storage Provider" -KeyExportPolicy Exportable -NotAfter(Get-Date).AddYears(1) -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3,1.3.6.1.4.1.311.10.3.6","2.5.29.19 = {text}")
        $CodeSignCertPath = Join-Path $Env:TEMP "CodeSignRoot.cer"
        Export-Certificate -Type CERT -Cert $CodeSignCert -FilePath $CodeSignCertPath
        CertUtil.exe -addstore Root $CodeSignCertPath
        CertUtil.exe -addstore trustedpublisher $CodeSignCertPath
        Export-PfxCertificate -Cert $CodeSignCert -Password $PfxPassword -FilePath $CertPath
        Remove-Item $CodeSignCertPath
        Remove-Item $CodeSignCert.PSPath
    } else {
        Write-Error "$CertPath does not exist!"
    }
}

# Artifact paths.
$RootDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ArtifactsDir = Join-Path $RootDir "artifacts" "bin" "$($Arch)_$($Config)"
$XdpDir = Join-Path $ArtifactsDir "xdp"
$XdpSys = Join-Path $XdpDir "xdp.sys"
$XdpInf = Join-Path $XdpDir "xdp.inf"
$XdpCat = Join-Path $XdpDir "xdp.cat"
$FndisSys = Join-Path $ArtifactsDir "fndis" "fndis.sys"
$XdpMpDir = Join-Path $ArtifactsDir "xdpmp"
$XdpMpSys = Join-Path $XdpMpDir "xdpmp.sys"
$XdpMpInf = Join-Path $XdpMpDir "xdpmp.inf"
$XdpMpCat = Join-Path $XdpMpDir "xdpmp.cat"
$XdpFnMpDir = Join-Path $ArtifactsDir "xdpfnmp"
$XdpFnMpSys = Join-Path $XdpFnMpDir "xdpfnmp.sys"
$XdpFnMpInf = Join-Path $XdpFnMpDir "xdpfnmp.inf"
$XdpFnMpCat = Join-Path $XdpFnMpDir "xdpfnmp.cat"

# Verify all the files are present.
if (!(Test-Path $XdpSys)) { Write-Error "$XdpSys does not exist!" }
if (!(Test-Path $XdpInf)) { Write-Error "$XdpInf does not exist!" }
if (!(Test-Path $FndisSys)) { Write-Error "$FndisSys does not exist!" }
if (!(Test-Path $XdpMpSys)) { Write-Error "$XdpMpSys does not exist!" }
if (!(Test-Path $XdpMpInf)) { Write-Error "$XdpMpInf does not exist!" }
if (!(Test-Path $XdpFnMpSys)) { Write-Error "$XdpFnMpSys does not exist!" }
if (!(Test-Path $XdpFnMpInf)) { Write-Error "$XdpFnMpInf does not exist!" }

# Sign the driver files.
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $XdpSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $FndisSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $XdpMpSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $XdpFnMpSys
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }

# Build up the catalogs.
& $Inf2CatToolPath /driver:$XdpDir /os:10_x64
if ($LastExitCode) { Write-Error "inf2cat.exe exit code: $LastExitCode" }
& $Inf2CatToolPath /driver:$XdpMpDir /os:10_x64
if ($LastExitCode) { Write-Error "inf2cat.exe exit code: $LastExitCode" }
& $Inf2CatToolPath /driver:$XdpFnMpDir /os:10_x64
if ($LastExitCode) { Write-Error "inf2cat.exe exit code: $LastExitCode" }

# Sign the catalogs.
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $XdpCat
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $XdpMpCat
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
& $SignToolPath sign /f $CertPath -p "placeholder" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $XdpFnMpCat
if ($LastExitCode) { Write-Error "signtool.exe exit code: $LastExitCode" }
