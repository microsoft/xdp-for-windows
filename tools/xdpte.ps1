#
# Helper script to deploy and run TAEF tests.
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
    [switch]$Loop = $false,

    [Parameter(Mandatory=$false)]
    [int]$Priority = 4
)

if (-not [string]::IsNullOrEmpty($Connect)) {
    opend $Connect
}

if ($Deploy) {
    testd "$env:_nttree\onecoreuap\prebuilt\windows\xdpfunctionaltests.testmd" `
        -AlternatePackageRoots `
            "$env:_nttree\onecoreuap\prebuilt\test\$env:_BuildArch\$env:_BuildType"

    cmdd "verifier.exe /standard /driver xdp.sys xdpfnmp.sys"
    cmdd "shutdown.exe /r /f /t 0"
}

if ($Redeploy) {
    putd $env:_nttree\xdp\test\xdpvrf\*
    putd $env:_nttree\xdp\test\functional\*
}

if ($Run) {
    #
    # Since we don't have ETLs captured automatically with TAEF yet, enable
    # verbose IFR and increase the IFR buffer size.
    #
    cmdd "reg add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v VerboseOn /t REG_DWORD /f /d 1"
    cmdd "reg add HKLM\SYSTEM\CurrentControlSet\Services\xdp\Parameters /v LogPages /t REG_DWORD /f /d 64"

    if ($Loop) {
        $TeParams += " /testmode:Loop"
    }

    $TeParams += " /select:`"@Priority<=$Priority`""

    cmdd "te.exe c:\data\test\bin\xdpfunctionaltests.dll $TeParams"
}
