#
# Helper script to start/stop XDP traces.
#

param (
    [Parameter(Mandatory=$false)]
    [switch]$Start = $false,

    [Parameter(Mandatory=$false)]
    [switch]$Stop = $false,

    [Parameter(Mandatory=$false)]
    [switch]$IfrVerboseEnable = $false,

    [Parameter(Mandatory=$false)]
    [switch]$IfrVerboseDisable = $false,

    [Parameter(Mandatory=$false)]
    [int]$Level = 5,

    [Parameter(Mandatory=$false)]
    [string]$Flags = "0xFFFFFFFFFFFFFFFF",

    [Parameter(Mandatory=$false)]
    [string]$OutFile = ".\xdptrace.etl"
)

$TraceName = "xdptrace"
$XdpWppGuid = "{D6143B5C-9FD6-44BA-BA02-FAD9EA0C263D}"

#
# Start or stop a WPP trace session.
#

if ($Start) {
    logman.exe create trace $TraceName -o $OutFile -p $XdpWppGuid $Flags $Level -ets
}

if ($Stop) {
    logman.exe stop $TraceName -ets
}

#
# Enable or disable verbose IFR logging. IFR verbose logging is disabled by default.
#

if ($IfrVerboseEnable) {
    reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v VerboseOn /t REG_DWORD /d 1
}

if ($IfrVerboseDisable) {
    reg.exe delete HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v VerboseOn /f
}
