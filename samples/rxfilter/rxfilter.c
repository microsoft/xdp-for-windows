//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <xdpapi.h>
#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#pragma warning(pop)
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

CONST CHAR *UsageText =
"rxfilter.exe -IfIndex <IfIndex> -BpfProgram <path.sys> -ProgramName <name> [OPTIONS]\n"
"\n"
"Filters RX traffic using an eBPF XDP program. The specified compiled eBPF\n"
"program (.sys) is loaded and attached to the given network interface.\n"
"\n"
"OPTIONS:\n"
"\n"
"   -IfIndex <IfIndex>\n"
"       The network interface index to attach the program to.\n"
"\n"
"   -BpfProgram <path.sys>\n"
"       Path to the compiled eBPF program (.sys native driver).\n"
"\n"
"   -ProgramName <name>\n"
"       The function name of the eBPF program entry point.\n"
"\n"
"   -UdpDstPort <Port>\n"
"       For udp_filter: the UDP destination port to filter.\n"
"\n"
"   -Action <Action>\n"
"       For udp_filter: the action on match (Drop or Pass). Default: Drop.\n"
"\n"
"Examples:\n"
"\n"
"   rxfilter.exe -IfIndex 6 -BpfProgram rxfilter_drop_all.sys -ProgramName drop_all\n"
"   rxfilter.exe -IfIndex 6 -BpfProgram rxfilter_udp.sys -ProgramName udp_filter -UdpDstPort 53 -Action Drop\n"
"   rxfilter.exe -IfIndex 6 -BpfProgram rxfilter_l2fwd.sys -ProgramName l2fwd\n"
;

static UINT32 IfIndex;
static const CHAR *BpfProgramPath;
static const CHAR *ProgramName;
static UINT16 UdpDstPort;
static BOOLEAN UdpDstPortSet;
static UINT32 MatchAction; // xdp_action_t: 2=DROP, 1=PASS

static
VOID
ParseArgs(
    INT ArgC,
    CHAR **ArgV
    )
{
    INT i = 1;

    IfIndex = 0;
    BpfProgramPath = NULL;
    ProgramName = NULL;
    UdpDstPort = 0;
    UdpDstPortSet = FALSE;
    MatchAction = 2; // XDP_DROP

    while (i < ArgC) {
        if (!_stricmp(ArgV[i], "-IfIndex")) {
            if (++i >= ArgC) {
                LOGERR("Missing IfIndex");
                goto Usage;
            }
            IfIndex = atoi(ArgV[i]);
        } else if (!_stricmp(ArgV[i], "-BpfProgram")) {
            if (++i >= ArgC) {
                LOGERR("Missing BpfProgram path");
                goto Usage;
            }
            BpfProgramPath = ArgV[i];
        } else if (!_stricmp(ArgV[i], "-ProgramName")) {
            if (++i >= ArgC) {
                LOGERR("Missing ProgramName");
                goto Usage;
            }
            ProgramName = ArgV[i];
        } else if (!_stricmp(ArgV[i], "-UdpDstPort")) {
            if (++i >= ArgC) {
                LOGERR("Missing UdpDstPort");
                goto Usage;
            }
            UdpDstPort = (UINT16)atoi(ArgV[i]);
            UdpDstPortSet = TRUE;
        } else if (!_stricmp(ArgV[i], "-Action")) {
            if (++i >= ArgC) {
                LOGERR("Missing Action");
                goto Usage;
            }
            if (!_stricmp(ArgV[i], "Drop")) {
                MatchAction = 2; // XDP_DROP
            } else if (!_stricmp(ArgV[i], "Pass")) {
                MatchAction = 1; // XDP_PASS
            } else {
                LOGERR("Invalid Action (use Drop or Pass)");
                goto Usage;
            }
        } else {
            LOGERR("Unexpected parameter \"%s\"", ArgV[i]);
            goto Usage;
        }

        ++i;
    }

    if (IfIndex == 0) {
        LOGERR("IfIndex is required");
        goto Usage;
    }

    if (BpfProgramPath == NULL) {
        LOGERR("BpfProgram is required");
        goto Usage;
    }

    if (ProgramName == NULL) {
        LOGERR("ProgramName is required");
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
    struct bpf_object *BpfObject = NULL;
    struct bpf_program *BpfProgram = NULL;
    int ProgramFd;
    int Result;

    ParseArgs(argc, argv);

    BpfObject = bpf_object__open(BpfProgramPath);
    if (BpfObject == NULL) {
        LOGERR("bpf_object__open(%s) failed", BpfProgramPath);
        return 1;
    }

    Result = bpf_object__load(BpfObject);
    if (Result != 0) {
        LOGERR("bpf_object__load failed: %d", Result);
        goto Exit;
    }

    BpfProgram = bpf_object__find_program_by_name(BpfObject, ProgramName);
    if (BpfProgram == NULL) {
        LOGERR("Program '%s' not found in %s", ProgramName, BpfProgramPath);
        goto Exit;
    }

    ProgramFd = bpf_program__fd(BpfProgram);
    if (ProgramFd < 0) {
        LOGERR("bpf_program__fd failed");
        goto Exit;
    }

    //
    // If map-based configuration is needed, populate the maps before
    // attaching the program.
    //
    if (UdpDstPortSet) {
        fd_t PortMapFd = bpf_object__find_map_fd_by_name(BpfObject, "port_map");
        fd_t ActionMapFd = bpf_object__find_map_fd_by_name(BpfObject, "action_map");

        if (PortMapFd < 0) {
            LOGERR("port_map not found in eBPF program");
            goto Exit;
        }
        if (ActionMapFd < 0) {
            LOGERR("action_map not found in eBPF program");
            goto Exit;
        }

        UINT32 Key = 0;
        UINT16 Port = htons(UdpDstPort);

        Result = bpf_map_update_elem(PortMapFd, &Key, &Port, 0);
        if (Result != 0) {
            LOGERR("Failed to set port_map: %d", Result);
            goto Exit;
        }

        Result = bpf_map_update_elem(ActionMapFd, &Key, &MatchAction, 0);
        if (Result != 0) {
            LOGERR("Failed to set action_map: %d", Result);
            goto Exit;
        }

        printf("Configured UDP filter: port=%u action=%s\n",
            UdpDstPort, MatchAction == 2 ? "DROP" : "PASS");
    }

    Result = bpf_xdp_attach(IfIndex, ProgramFd, 0, NULL);
    if (Result != 0) {
        LOGERR("bpf_xdp_attach(IfIndex=%u) failed: %d", IfIndex, Result);
        goto Exit;
    }

    printf("eBPF program '%s' attached to IfIndex %u. Press Ctrl+C to stop.\n",
        ProgramName, IfIndex);

    //
    // Let the eBPF program filter frames until this process is terminated.
    //
    Sleep(INFINITE);

    //
    // Detach on exit (reached if INFINITE sleep is interrupted).
    //
    bpf_xdp_detach(IfIndex, 0, NULL);

Exit:
    if (BpfObject != NULL) {
        bpf_object__close(BpfObject);
    }

    return Result == 0 ? 0 : 1;
}
