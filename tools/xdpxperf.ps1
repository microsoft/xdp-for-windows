#
# Helper script to start/stop xperf traces.
#

param (
    [Parameter(Mandatory=$false)]
    [switch]$Start = $false,

    [Parameter(Mandatory=$false)]
    [switch]$Stop = $false,

    [Parameter(Mandatory=$false)]
    [string]$OutFile = ".\xdpxperf.etl"
)

#
# Start or stop a WPP trace session.
#

if ($Start) {
    xperf.exe -on "PROC_THREAD+LOADER+DPC+INTERRUPT+PROFILE" -stackwalk profile
}

if ($Stop) {
    xperf.exe -d $OutFile | Out-Null
}
