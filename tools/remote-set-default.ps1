<#

.SYNOPSIS
Set or clear a default remote test machine for the current PowerShell
session, so that subsequent XDP test scripts (functional.ps1,
spinxsk.ps1, ...) don't need a -ComputerName argument.

.PARAMETER ComputerName
The remote test machine. Use '' or omit to clear.

.PARAMETER Clear
Clear the session default and any cached credentials.

.EXAMPLE
# Set default; subsequent commands auto-route to this machine:
.\tools\remote-set-default.ps1 mytesthost
.\tools\functional.ps1 -ListTestCases

.EXAMPLE
# Clear the default and any cached credentials:
.\tools\remote-set-default.ps1 -Clear

#>

param (
    [Parameter(Position = 0)]
    [string]$ComputerName,

    [Parameter()]
    [switch]$Clear
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

if ($Clear -or [string]::IsNullOrWhiteSpace($ComputerName)) {
    Remove-Variable -Name XdpRemoteDefault -Scope Global -ErrorAction SilentlyContinue
    Remove-Variable -Name XdpRemoteCredentialCache -Scope Global -ErrorAction SilentlyContinue
    Write-Host "Cleared session default remote computer and cached credentials." -ForegroundColor Green
    return
}

Set-Variable -Name XdpRemoteDefault -Scope Global -Value $ComputerName
Write-Host "Default remote computer for this session: $ComputerName" -ForegroundColor Green
Write-Host "Subsequent .\tools\*.ps1 invocations will run against this machine."
