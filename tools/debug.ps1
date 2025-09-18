param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Paths & config
$DownloadsDir = 'C:\Users\jackhe\Downloads'
$TracingDir   = 'C:\Users\jackhe\Developer\tracing'
$VmName       = 'dev-vm-password=test3'
$Checkpoint   = 'qtip-pr'
$GuestDest    = 'C:\Users\User\Desktop\xdp-for-windows\artifacts\bin\x64_Debug'

# --- Pick the newest "bin_Debug_x64 (N).zip" by highest N ---
Write-Host "Scanning $DownloadsDir for 'bin_Debug_x64 (N).zip' files..." -ForegroundColor Cyan
$zipCandidates = Get-ChildItem -LiteralPath $DownloadsDir -File |
    Where-Object { $_.Name -match '^bin_Debug_x64 \((\d+)\)\.zip$' } |
    ForEach-Object {
        # Extract N from the filename
        $null = [regex]::Match($_.Name, '^bin_Debug_x64 \((\d+)\)\.zip$')
        [pscustomobject]@{
            FileInfo = $_
            N       = [int]$Matches[1]
        }
    } |
    Sort-Object -Property N -Descending

if (-not $zipCandidates -or $zipCandidates.Count -eq 0) {
    throw "No files matching 'bin_Debug_x64 (N).zip' found in $DownloadsDir."
}

$Chosen = $zipCandidates[0]
$ZipPath = $Chosen.FileInfo.FullName
Write-Host "Selected zip: $($Chosen.FileInfo.Name) (N=$($Chosen.N))" -ForegroundColor Yellow

# --- Ensure tracing dir exists ---
if (-not (Test-Path -LiteralPath $TracingDir)) {
    New-Item -ItemType Directory -Path $TracingDir | Out-Null
}

# --- Extract & merge contents of x64_Debug into $TracingDir (overwrite) ---
Write-Host "Extracting $ZipPath..." -ForegroundColor Cyan
$TempExtract = Join-Path $env:TEMP ("trace_extract_{0}" -f ([guid]::NewGuid()))
New-Item -ItemType Directory -Path $TempExtract | Out-Null
Expand-Archive -LiteralPath $ZipPath -DestinationPath $TempExtract -Force

# Prefer x64_Debug subfolder if present; else use the extraction root
$DebugDir = Join-Path $TempExtract 'x64_Debug'
$SourceToCopy = if (Test-Path -LiteralPath $DebugDir) { $DebugDir } else { $TempExtract }

Write-Host "Merging contents from '$SourceToCopy' into '$TracingDir' (overwrite)..." -ForegroundColor Cyan
# Copy all content (not mirror), overwrite clashing names
# /E  : include subdirs
# /IS : include same files (treat same size/date as changed so overwrite)
# /IT : include tweaked
# /R:1 /W:1 : minimal retries
robocopy $SourceToCopy $TracingDir /E /IS /IT /R:1 /W:1 | Out-Null

# Cleanup temp extract
Remove-Item -LiteralPath $TempExtract -Recurse -Force

# --- Revert VM to checkpoint ---
Write-Host "Reverting VM '$VmName' to checkpoint '$Checkpoint'..." -ForegroundColor Cyan
try {
    Stop-VM -Name $VmName -TurnOff -Force -ErrorAction SilentlyContinue | Out-Null
} catch {}

$cp = Get-VMCheckpoint -VMName $VmName | Where-Object { $_.Name -eq $Checkpoint }
if (-not $cp) {
    throw "Checkpoint '$Checkpoint' not found on VM '$VmName'."
}
Restore-VMCheckpoint -VMCheckpoint $cp -Confirm:$false

# --- Start VM & wait for heartbeat ---
Write-Host "Starting VM..." -ForegroundColor Cyan
Start-VM -Name $VmName | Out-Null

Write-Host "Waiting for VM heartbeat..." -ForegroundColor Cyan
$deadline = (Get-Date).AddMinutes(5)
do {
    Start-Sleep -Seconds 3
    $vm = Get-VM -Name $VmName
    $ok = ($vm.Heartbeat -eq 'Ok' -or $vm.Heartbeat -eq 'OkApplicationsUnknown')
} until ($ok -or (Get-Date) -gt $deadline)

if (-not $ok) {
    throw "VM did not report a healthy heartbeat within 5 minutes."
}

# --- PowerShell Direct to copy files to guest ---
Write-Host "Opening PowerShell Direct session into '$VmName'..." -ForegroundColor Cyan
$username = "User"
$password = "test3"
$cred = New-Object System.Management.Automation.PSCredential($username, (ConvertTo-SecureString $password -AsPlainText -Force))

$session = $null
for ($i=0; $i -lt 10 -and -not $session; $i++) {
    try {
        $session = New-PSSession -VMName $VmName -Credential $cred -ErrorAction Stop
    } catch {
        Start-Sleep -Seconds 3
    }
}
if (-not $session) {
    throw "Failed to create a PowerShell Direct session to '$VmName'."
}

try {
    Invoke-Command -Session $session -ScriptBlock {
        param($Dest)
        New-Item -ItemType Directory -Path $Dest -Force | Out-Null
    } -ArgumentList $GuestDest

    Write-Host "Copying $TracingDir to ${VmName}:${GuestDest} ..." -ForegroundColor Cyan
    Copy-Item -ToSession $session -Path (Join-Path $TracingDir '*') -Destination $GuestDest -Recurse -Force
}
finally {
    if ($session) { Remove-PSSession $session }
}

# --- Launch WinDbg and attach to VM kernel ---
# Write-Host "Launching WinDbg and attaching to '$VmName' kernel..." -ForegroundColor Cyan
# $windbgPreview = "$env:LOCALAPPDATA\Microsoft\WindowsApps\WinDbgX.exe"
# $windbgClassic = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\windbg.exe"

# $dbgExe = if (Test-Path $windbgPreview) { $windbgPreview }
#           elseif (Test-Path $windbgClassic) { $windbgClassic }
#           else { $null }

# if (-not $dbgExe) {
#     Write-Warning "WinDbg not found. Install WinDbg (Preview) from the Microsoft Store or the Windows SDK debuggers."
# } else {
#     $args = @('-k', 'hyperv', '-vmname', $VmName)
#     Start-Process -FilePath $dbgExe -ArgumentList $args
#     Write-Host "WinDbg launched."
# }

Write-Host "All steps completed successfully." -ForegroundColor Green
