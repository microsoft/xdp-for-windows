Write-Host "Make sure you run this in an admin developer powershell window"

Write-Host "Make sure to run .\tools\setup.ps1 -Install xdp -XdpInstaller INF on the test VM"


# Ensure a test-signing certificate exists (one-time setup, requires admin)
$CertSubject = "CN=XDP Test"
$ExistingCert = Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert | Where-Object { $_.Subject -eq $CertSubject }
if (-not $ExistingCert) {
    Write-Host "Creating test-signing certificate..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject -CertStoreLocation Cert:\LocalMachine\My
    foreach ($storeName in @("Root", "TrustedPublisher")) {
        $store = Get-Item "Cert:\LocalMachine\$storeName"
        $store.Open("ReadWrite")
        $store.Add($cert)
        $store.Close()
    }
    Write-Host "Test-signing certificate created and trusted."
} else {
    Write-Host "Test-signing certificate already exists."
}

# Restore NuGet packages
msbuild xdp.sln /t:restore /p:RestoreConfigFile=tools\nuget.config /p:Configuration=Debug /p:Platform=x64

# Build core XDP components (Binary + Catalog stages)
msbuild xdp.sln /m /t:onebranch /p:Configuration=Debug /p:Platform=x64 /p:SignMode=TestSign /p:IsAdmin=true /p:BuildStage=Binary /nodeReuse:false
msbuild xdp.sln /m /t:onebranch /p:Configuration=Debug /p:Platform=x64 /p:SignMode=TestSign /p:IsAdmin=true /p:BuildStage=Catalog /nodeReuse:false

# Build user-mode samples
$SolutionDir = (Resolve-Path "$PSScriptRoot\..").Path + "\"
msbuild samples\rxfilter\rxfilter.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SignMode=TestSign /p:SolutionDir=$SolutionDir /nodeReuse:false
msbuild samples\xskfwd\xskfwd.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SignMode=TestSign /p:SolutionDir=$SolutionDir /nodeReuse:false
msbuild samples\xskmaprx\xskmaprx.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SignMode=TestSign /p:SolutionDir=$SolutionDir /nodeReuse:false
msbuild samples\xskrestricted\xskrestricted.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SignMode=TestSign /p:SolutionDir=$SolutionDir /nodeReuse:false

# Export the signing cert into the artifacts directory for use on test VMs
$ArtifactsDir = "$SolutionDir\artifacts\bin\x64_Debug"
$WdkCert = Get-AuthenticodeSignature "$ArtifactsDir\xdp\xdp.cat" | Select-Object -ExpandProperty SignerCertificate
Export-Certificate -Cert $WdkCert -FilePath "$ArtifactsDir\xdp\xdp.cer" -Force | Out-Null
Write-Host "Exported signing cert to artifacts\bin\x64_Debug\xdp\xdp.cer"

# Copy debug CRT DLLs into the artifacts directory
$VsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$DebugCrtDir = Get-ChildItem "$VsPath\VC\Redist\MSVC" -Directory |
    ForEach-Object { Get-ChildItem "$($_.FullName)\debug_nonredist\x64\Microsoft.VC*.DebugCRT" -Directory -ErrorAction SilentlyContinue } |
    Select-Object -Last 1
$DestDir = "$ArtifactsDir\test\debugcrt"
New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
Copy-Item "$($DebugCrtDir.FullName)\*" $DestDir -Force
Write-Host "Copied debug CRT from $($DebugCrtDir.FullName) to $DestDir"

# Copy xdp-setup.ps1 into the artifacts directory
Copy-Item "$SolutionDir\src\xdpruntime\xdp-setup.ps1" "$ArtifactsDir\" -Force