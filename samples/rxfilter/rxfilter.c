//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <xdpapi.h>

CONST CHAR *UsageText =
"rxfilter.exe -IfIndex <IfIndex> -QueueId <QueueId> [OPTIONS] RULE_PARAMS\n"
"\n"
"Filters RX traffic using an XDP program. Traffic that does not match the\n"
"filter will be allowed to pass through.\n"
"\n"
"RULE_PARAMS:\n"
"\n"
"   -MatchType <Type>\n"
"\n"
"       The frame filter type. Each of the supported types and their parameters\n"
"       are listed under FILTER_TYPES.\n"
"\n"
"   -Action <Action>\n"
"\n"
"       The action to perform on matching frames:\n"
"       - Pass: Allow the frame to pass through XDP\n"
"       - Drop: Drop the frame\n"
"       - L2Fwd: Forward the frame back to the L2 sender\n"
"\n"
"FILTER_TYPES:\n"
"\n"
"   All\n"
"\n"
"       Matches all frames.\n"
"\n"
"   UdpDstPort\n"
"\n"
"       Matches all UDP frames with the specified destination port\n"
"\n"
"       -UdpDstPort <Port>\n"
"           The UDP destination port\n"
"\n"
"OPTIONS:\n"
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
"   rxfilter.exe -IfIndex 6 -QueueId 0 -MatchType All -Action Drop\n"
"   rxfilter.exe -IfIndex 6 -QueueId 0 -MatchType UdpDstPort -UdpDstPort 53 -Action Drop\n"
;

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

UINT32 IfIndex;
UINT32 QueueId;
XDP_RULE Rule;
UINT32 ProgramFlags;

VOID
ParseArgs(
    INT ArgC,
    CHAR **ArgV
    )
{
    INT i = 1;

    IfIndex = MAXUINT32;
    QueueId = MAXUINT32;
    ProgramFlags = 0;

    ZeroMemory(&Rule, sizeof(Rule));
    Rule.Match = XDP_MATCH_ALL;
    Rule.Action = XDP_PROGRAM_ACTION_PASS;

    while (i < ArgC) {
        if (!_stricmp(ArgV[i], "-IfIndex")) {
            if (++i >= ArgC) {
                LOGERR("Missing IfIndex");
                goto Usage;
            }
            IfIndex = atoi(ArgV[i]);
        } else if (!_stricmp(ArgV[i], "-QueueId")) {
            if (++i >= ArgC) {
                LOGERR("Missing QueueId");
                goto Usage;
            }
            QueueId = atoi(ArgV[i]);
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
        } else if (!_stricmp(ArgV[i], "-MatchType")) {
            if (++i >= ArgC) {
                LOGERR("Missing MatchType");
                goto Usage;
            }
            if (!_stricmp(ArgV[i], "All")) {
                Rule.Match = XDP_MATCH_ALL;
            } else if (!_stricmp(ArgV[i], "UdpDstPort")) {
                Rule.Match = XDP_MATCH_UDP_DST;
            } else {
                LOGERR("Invalid MatchType");
                goto Usage;
            }
        } else if (!_stricmp(ArgV[i], "-Action")) {
            if (++i >= ArgC) {
                LOGERR("Missing Action");
                goto Usage;
            }
            if (!_stricmp(ArgV[i], "Pass")) {
                Rule.Action = XDP_PROGRAM_ACTION_PASS;
            } else if (!_stricmp(ArgV[i], "Drop")) {
                Rule.Action = XDP_PROGRAM_ACTION_DROP;
            } else if (!_stricmp(ArgV[i], "L2Fwd")) {
                Rule.Action = XDP_PROGRAM_ACTION_L2FWD;
            } else {
                LOGERR("Invalid Action");
                goto Usage;
            }
        } else if (!_stricmp(ArgV[i], "-UdpDstPort")) {
            if (++i >= ArgC) {
                LOGERR("Missing UdpDstPort");
                goto Usage;
            }
            if (Rule.Match == XDP_MATCH_UDP_DST) {
                Rule.Pattern.Port = _byteswap_ushort((UINT16)atoi(ArgV[i]));
            } else {
                LOGERR("Unexpected UdpDstPort");
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

    if (QueueId == MAXUINT32) {
        LOGERR("QueueId is required");
        goto Usage;
    }

    return;

Usage:

    printf(UsageText);
    exit(1);
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    const XDP_API_TABLE *XdpApi;
    HRESULT Result;
    HANDLE Program;
    CONST XDP_HOOK_ID XdpInspectRxL2 = {
        XDP_HOOK_L2,
        XDP_HOOK_RX,
        XDP_HOOK_INSPECT,
    };

    //
    // Parse the command line arguments.
    //
    ParseArgs(argc, argv);

    Result = XdpOpenApi(XDP_API_VERSION_1, &XdpApi);
    if (FAILED(Result)) {
        LOGERR("XdpOpenApi failed: %x", Result);
        return 1;
    }

    //
    // Create an XDP program using the parsed rule at the L2 inspect hook point.
    //
    Result =
        XdpApi->XdpCreateProgram(
            IfIndex, &XdpInspectRxL2, QueueId, ProgramFlags, &Rule, 1, &Program);
    if (FAILED(Result)) {
        LOGERR("XdpCreateProgram failed: %x", Result);
        return 1;
    }

    //
    // Let XDP filter frames until this process is terminated.
    //
    Sleep(INFINITE);

    return 0;
}
