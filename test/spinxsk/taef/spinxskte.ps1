#
# Helper script to deploy and run the spinxsk TAEF test.
#

param (
    [Parameter(Mandatory=$false)]
    [string]$Connect = $null,

    [Parameter(Mandatory=$false)]
    [switch]$Deploy = $false,

    [Parameter(Mandatory=$false)]
    [switch]$Redeploy = $false,

    [Parameter(Mandatory=$false)]
    [switch]$Run = $false,

    [Parameter(Mandatory=$false)]
    [switch]$Fndis = $false,

    [Parameter(Mandatory=$false)]
    [int]$Minutes = 2
)

if (-not [string]::IsNullOrEmpty($Connect)) {
    opend $Connect
}

if ($Deploy) {
    testd -testmd "$env:_nttree\onecoreuap\prebuilt\windows\spinxsk.testmd" `
        -AlternatePackageRoots `
            "$env:_nttree\onecoreuap\prebuilt\test\$env:_BuildArch\$env:_BuildType " `
        -AdditionalTaefParameters "/p:Fndis=$Fndis"

    #
    # Standard flags with low resources simulation.
    #
    # 600 - Failure probability (600/10000 = 6%)
    # ""  - Pool tag filter
    # ""  - Application filter
    # 0   - Delay (in minutes) after boot until simulation engages
    #
    # WARNING: spinxsk.dll may fail to load xdp.sys due to low resources simulation.
    #          Simply re-run spinxsk.dll to try again.
    #
    cmdd "verifier.exe /standard /faults 600 `"`" `"`" 0 /driver xdp.sys"
    cmdd "shutdown.exe /r /f /t 0"
}

if ($Redeploy) {
    putd $env:_nttree\xdp\test\xdpvrf\*
    putd $env:_nttree\xdp\test\spinxsk\*
    putd $env:_nttree\xdp\test\xdpmp\*
    putd $env:_nttree\xdp\test\fakendis\*
}

if ($Run) {
    cmdd "powershell.exe /c `".\te.exe spinxsk.dll /p:Minutes=$Minutes /p:Fndis=$Fndis | tee spinxskte.log`""
}
