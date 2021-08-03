#
# Archive layout:
#
#   readme.md
#   info.txt
#   \bin
#   \include
#   \lib
#   \symbols
#   \samples
#

if ([string]::IsNullOrEmpty($env:_NTROOT) -or
    [string]::IsNullOrEmpty($env:_NTTREE) -or
    [string]::IsNullOrEmpty($env:OBJECT_ROOT) -or
    [string]::IsNullOrEmpty($env:PUBLIC_ROOT)) {
    Write-Host "Necessary environment variables not present. Are you running in razzle?"
    return
}

if (git status --porcelain) {
    Write-Warning "Uncommitted changes detected."
}

$gitCommitHash = git rev-parse HEAD
$gitAbbrevCommitHash = git rev-parse --short HEAD
$gitBranch = git branch --show-current
$buildFlavor = "$($env:_BuildArch)$($env:_BuildType)"

foreach ($KitType in "sdk", "ddk") {
    $dstPath = "$gitAbbrevCommitHash-$buildFlavor-$KitType"
    New-Item -Path $dstPath -ItemType Directory > $null

    #
    # bin
    #
    $binSrcPath = "$env:_NTTREE\xdp"
    $binDstPath = "$dstPath\bin"
    $binFiles = @(
        "xdp.inf",
        "xdp.cat",
        "xdp.sys",
        "msxdp.dll",
        "test\pktcmd\pktcmd.exe",
        "test\xskbench\xskbench.exe")
    New-Item -Path $binDstPath -ItemType Directory > $null
    foreach ($file in $binFiles) {
        copy $binSrcPath\$file $binDstPath
    }
    copy "$env:BASEDIR\tools\sha2-testroot.cer" $binDstPath

    #
    # include
    #
    $includeSrcPath = "$env:PUBLIC_ROOT\onecore\internal\minwin\priv_sdk\inc\xdp"
    $includeDstPath = "$dstPath\include"
    New-Item -Path $includeDstPath -ItemType Directory > $null
    Copy-Item -Recurse "$includeSrcPath\*" $includeDstPath
    #
    # Exclude the dual-licensed Linux XSK helper header, as its causing unnecessary
    # friction when sharing with IHVs who don't need it.
    #
    Remove-Item -Path $includeDstPath\sdk\afxdp_helper_linux.h
    if ($KitType -ne "ddk") {
        Remove-Item -Recurse -Path $includeDstPath\ddk
    }

    #
    # lib
    #
    $libDstPath = "$dstPath\lib"
    New-Item -Path $libDstPath -ItemType Directory > $null
    link /cvtcil /out:$libDstPath\msxdp.lib "$env:OBJECT_ROOT\onecore\net\xdp\core\dll\msxdp\retail\$env:O\msxdp.lib"
    if ($KitType -eq "ddk") {
        link /cvtcil /out:$libDstPath\xdpnmr.lib "$env:OBJECT_ROOT\onecore\net\xdp\lib\nmr\$env:O\xdpnmr.lib"
    }

    #
    # symbols
    #
    $symSrcPath = "$env:_NTTREE\symbols.pri\xdp"
    $symDstPath = "$dstPath\symbols"
    $symFiles = @(
        "sys\xdp.pdb",
        "dll\msxdp.pdb",
        "exe\pktcmd.pdb",
        "exe\xskbench.pdb")
    New-Item -Path $symDstPath -ItemType Directory > $null
    foreach ($file in $symFiles) {
        copy $symSrcPath\$file $symDstPath
    }

    #
    # samples
    #
    $samplesSrcPath = "$env:_NTROOT\onecore\net\xdp"
    $samplesDstPath = "$dstPath\samples"
    $samplesFiles = @(
        "test\xskbench\xskbench.c")
    New-Item -Path $samplesDstPath -ItemType Directory > $null
    foreach ($file in $samplesFiles) {
        copy $samplesSrcPath\$file $samplesDstPath
    }
    if ($KitType -eq "ddk") {
        $xdpMpSrcPath = "$samplesSrcPath\test\xdpmp"
        $xdpMpDstPath = "$dstPath\samples\xdpmp"
        New-Item -Path $xdpMpDstPath -ItemType Directory > $null
        Copy-Item -Recurse "$xdpMpSrcPath\*.c" $xdpMpDstPath
        Copy-Item -Recurse "$xdpMpSrcPath\*.h" $xdpMpDstPath
        Copy-Item "$xdpMpSrcPath\inf\xdpmp.inx" $xdpMpDstPath
    }

    #
    # readme
    #
    copy $samplesSrcPath\readme.md $dstPath

    #
    # commit/build info
    #
    $gitCommitHash >> $dstPath\info.txt
    $gitBranch >> $dstPath\info.txt
    $buildFlavor >> $dstPath\info.txt

    Compress-Archive -Path $dstPath -DestinationPath ".\$dstPath.zip"

    Remove-Item -Path $dstPath -Recurse -Force

    Write-Host "Created $dstPath.zip"
}
