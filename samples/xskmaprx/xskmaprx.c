//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Sample program demonstrating XSKMAP-based RX queue steering.
//
// This sample creates an XSKMAP, binds XSK sockets to each specified RX queue,
// inserts the sockets into the map keyed by queue ID, and creates an XDP program
// that redirects matching traffic to the XSK for the current receive queue.
//

#include <xdpapi.h>
#include <afxdp.h>
#include <ws2tcpip.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

CONST CHAR *UsageText =
"xskmaprx.exe -IfIndex <IfIndex> -QueueCount <Count> [OPTIONS]\n"
"\n"
"Receives traffic from multiple RX queues using an XSKMAP to steer packets\n"
"to per-queue XSK sockets.\n"
"\n"
"OPTIONS:\n"
"\n"
"   -UdpDstPort <Port>\n"
"\n"
"       Match only UDP traffic with this destination port.\n"
"       Default: match all traffic.\n"
"\n"
"   -XdpMode <Mode>\n"
"\n"
"       The XDP interface provider mode:\n"
"       - System: The system determines the ideal XDP provider\n"
"       - Generic: Use the generic XDP interface provider\n"
"       - Native: Use the native XDP interface provider\n"
"       Default: System\n"
"\n"
"Examples:\n"
"\n"
"   xskmaprx.exe -IfIndex 6 -QueueCount 4\n"
"   xskmaprx.exe -IfIndex 6 -QueueCount 2 -UdpDstPort 9000\n"
;

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

UINT32 IfIndex;
UINT32 QueueCount;
UINT16 UdpDstPort;
BOOLEAN UseUdpMatch;
XDP_CREATE_PROGRAM_FLAGS ProgramFlags;

VOID
ParseArgs(
    INT ArgC,
    CHAR **ArgV
    )
{
    INT i = 1;

    IfIndex = MAXUINT32;
    QueueCount = 0;
    UdpDstPort = 0;
    UseUdpMatch = FALSE;
    ProgramFlags = XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES;

    while (i < ArgC) {
        if (!_stricmp(ArgV[i], "-IfIndex")) {
            if (++i >= ArgC) {
                LOGERR("Missing IfIndex");
                goto Usage;
            }
            IfIndex = atoi(ArgV[i]);
        } else if (!_stricmp(ArgV[i], "-QueueCount")) {
            if (++i >= ArgC) {
                LOGERR("Missing QueueCount");
                goto Usage;
            }
            QueueCount = atoi(ArgV[i]);
            if (QueueCount == 0 || QueueCount > 128) {
                LOGERR("QueueCount must be between 1 and 128");
                goto Usage;
            }
        } else if (!_stricmp(ArgV[i], "-UdpDstPort")) {
            if (++i >= ArgC) {
                LOGERR("Missing UdpDstPort");
                goto Usage;
            }
            UdpDstPort = _byteswap_ushort((UINT16)atoi(ArgV[i]));
            UseUdpMatch = TRUE;
        } else if (!_stricmp(ArgV[i], "-XdpMode")) {
            if (++i >= ArgC) {
                LOGERR("Missing XdpMode");
                goto Usage;
            }
            if (!_stricmp(ArgV[i], "Generic")) {
                ProgramFlags |= XDP_CREATE_PROGRAM_FLAG_GENERIC;
            } else if (!_stricmp(ArgV[i], "Native")) {
                ProgramFlags |= XDP_CREATE_PROGRAM_FLAG_NATIVE;
            } else if (_stricmp(ArgV[i], "System")) {
                LOGERR("Invalid XdpMode");
                goto Usage;
            }
        } else {
            LOGERR("Unexpected parameter \"%s\"", ArgV[i]);
            goto Usage;
        }

        ++i;
    }

    if (IfIndex == MAXUINT32) {
        LOGERR("IfIndex is required");
        goto Usage;
    }

    if (QueueCount == 0) {
        LOGERR("QueueCount is required");
        goto Usage;
    }

    return;

Usage:

    printf("%s", UsageText);
    exit(1);
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    XDP_STATUS XdpStatus;
    HANDLE XskMap = NULL;
    HANDLE Program = NULL;
    HANDLE *Sockets = NULL;
    const XDP_HOOK_ID XdpInspectRxL2 = {
        XDP_HOOK_L2,
        XDP_HOOK_RX,
        XDP_HOOK_INSPECT,
    };

    ParseArgs(argc, argv);

    //
    // Create the XSKMAP.
    //
    XdpStatus = XdpMapCreate(&XskMap, XDP_MAP_TYPE_XSKMAP);
    if (FAILED(XdpStatus)) {
        LOGERR("XdpMapCreate failed: %x", XdpStatus);
        return 1;
    }

    printf("Created XSKMAP\n");

    //
    // Allocate array to track socket handles.
    //
    Sockets = (HANDLE *)calloc(QueueCount, sizeof(HANDLE));
    if (Sockets == NULL) {
        LOGERR("Failed to allocate socket array");
        return 1;
    }

    //
    // Create and bind an XSK socket for each queue, then insert into the map.
    //
    for (UINT32 i = 0; i < QueueCount; i++) {
        XdpStatus = XskCreate(&Sockets[i]);
        if (FAILED(XdpStatus)) {
            LOGERR("XskCreate failed for queue %u: %x", i, XdpStatus);
            return 1;
        }

        XdpStatus = XdpMapInsert(XskMap, i, Sockets[i]);
        if (FAILED(XdpStatus)) {
            LOGERR("XdpMapInsert failed for queue %u: %x", i, XdpStatus);
            return 1;
        }

        printf("Inserted XSK for queue %u into XSKMAP\n", i);
    }

    //
    // Create the XDP rule using the XSKMAP.
    //
    XDP_RULE Rule;
    ZeroMemory(&Rule, sizeof(Rule));

    if (UseUdpMatch) {
        Rule.Match = XDP_MATCH_UDP_DST;
        Rule.Pattern.Port = UdpDstPort;
    } else {
        Rule.Match = XDP_MATCH_ALL;
    }

    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSKMAP_BY_QUEUEID;
    Rule.Redirect.Target = XskMap;

    XdpStatus =
        XdpCreateProgram(
            IfIndex, &XdpInspectRxL2, 0, ProgramFlags, &Rule, 1, &Program);
    if (FAILED(XdpStatus)) {
        LOGERR("XdpCreateProgram failed: %x", XdpStatus);
        return 1;
    }

    printf(
        "XDP program created. Redirecting %s traffic across %u queues.\n"
        "Press Ctrl+C to stop.\n",
        UseUdpMatch ? "matching UDP" : "all",
        QueueCount);

    //
    // Let XDP redirect frames until this process is terminated.
    //
    Sleep(INFINITE);

    return 0;
}
