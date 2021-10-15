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
# 0   - Delay (in minutes) after boot until simulation engages
#
# WARNING: xdp.sys itself may fail to load due to low resources simulation.
#
verifier.exe /standard /faults 599 "" "" 0 /driver xdp.sys
