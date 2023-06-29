<#

.SYNOPSIS
This provisions a CI agent. The 1ES managed agent pool executes this script
after starting a new agent VM and automatically reboots. The script must be
copied to the Azure storage blob used for provisioning.

#>

param (
)

Write-Host "Enable test signing"
bcdedit.exe /set testsigning on

Write-Host "Enable driver verifier"
#
# Standard flags with low resources simulation.
#
# 599 - Failure probability (599/10000 = 5.99%)
#       N.B. If left to the default value, roughly every 5 minutes verifier
#       will fail all allocations within a 10 second interval. This behavior
#       complicates the spinxsk socket setup statistics. Setting it to a
#       non-default value disables this behavior.
# ""  - Pool tag filter
# ""  - Application filter
# 1   - Delay (in minutes) after boot until simulation engages
#       This is the lowest value configurable via verifier.exe.
#
# WARNING: xdp.sys itself may fail to load due to low resources simulation.
#
verifier.exe /standard /faults 599 `"`" `"`" 1 /driver xdp.sys ebpfcore.sys

#
# Disable TDX and its dependent service NetBT. These drivers are implicated in
# some NDIS control path hangs.
#
Write-Host "Disable NetBT"
reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\netbt /v Start /d 4 /t REG_DWORD /f
Write-Host "Disable TDX"
reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\tdx /v Start /d 4 /t REG_DWORD /f

#
# WS 2016 has a service dependency from DHCP to TDX. DHCP works fine in CI
# without TDX, so remove the service dependency.
#
$Depends = (Get-ItemProperty -Path HKLM:System\CurrentControlSet\Services\dhcp -Name DependOnService).DependOnService
if ($Depends -contains "tdx") {
    Write-Host "Remove TDX dependency from DHCP"
    $Depends = $Depends | Where {$_ -ne "tdx"}
    Set-ItemProperty -Path HKLM:System\CurrentControlSet\Services\dhcp -Name DependOnService -Value $Depends
}

#Write-Host "Enable complete system crash dumps"
#reg.exe add HKLM\System\CurrentControlSet\Control\CrashControl /v CrashDumpEnabled /d 1 /t REG_DWORD /f
