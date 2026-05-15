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
// Each XSK is fully configured with UMEM, RX and fill rings, and activated so
// that redirected packets are received. The sample prints per-queue and total
// packet/byte counts periodically and on exit.
//

#include <xdpapi.h>
#include <afxdp.h>
#include <afxdp_helper.h>
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
"   -IcmpOnly\n"
"\n"
"       Match only ICMPv4 traffic (protocol 1). This allows ping\n"
"       experiments without disrupting other traffic on the interface.\n"
"\n"
"   -XdpMode <Mode>\n"
"\n"
"       The XDP interface provider mode:\n"
"       - System: The system determines the ideal XDP provider\n"
"       - Generic: Use the generic XDP interface provider\n"
"       - Native: Use the native XDP interface provider\n"
"       Default: System\n"
"\n"
"   -TimeoutSeconds <Seconds>\n"
"\n"
"       Exit cleanly after the given number of seconds. 0 (default) runs\n"
"       until the process is terminated.\n"
"\n"
"Examples:\n"
"\n"
"   xskmaprx.exe -IfIndex 6 -QueueCount 4\n"
"   xskmaprx.exe -IfIndex 6 -QueueCount 2 -UdpDstPort 9000\n"
"   xskmaprx.exe -IfIndex 6 -QueueCount 1 -IcmpOnly -TimeoutSeconds 30\n"
;

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

//
// Per-queue RX state.
//
#define RX_RING_SIZE 64
#define FRAME_SIZE 2048

typedef struct _QUEUE_CONTEXT {
    HANDLE Socket;
    XSK_RING RxRing;
    XSK_RING RxFillRing;
    UCHAR *Umem;
    UINT64 PacketsReceived;
    UINT64 BytesReceived;
    UINT64 PacketsDropped;
} QUEUE_CONTEXT;

UINT32 IfIndex;
UINT32 QueueCount;
UINT16 UdpDstPort;
BOOLEAN UseUdpMatch;
BOOLEAN UseIcmpMatch;
UINT32 TimeoutSeconds;
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
    UseIcmpMatch = FALSE;
    TimeoutSeconds = 0;
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
        } else if (!_stricmp(ArgV[i], "-IcmpOnly")) {
            UseIcmpMatch = TRUE;
        } else if (!_stricmp(ArgV[i], "-TimeoutSeconds")) {
            if (++i >= ArgC) {
                LOGERR("Missing TimeoutSeconds");
                goto Usage;
            }
            TimeoutSeconds = atoi(ArgV[i]);
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

static
XDP_STATUS
SetupQueueSocket(
    _In_ UINT32 QueueId,
    _Inout_ QUEUE_CONTEXT *Ctx
    )
{
    XDP_STATUS XdpStatus;
    XSK_UMEM_REG UmemReg = {0};
    UINT32 RingSize = RX_RING_SIZE;
    XSK_RING_INFO_SET RingInfo;
    UINT32 OptionLength;

    Ctx->PacketsReceived = 0;
    Ctx->BytesReceived = 0;

    //
    // Allocate UMEM: one frame per fill ring entry.
    //
    Ctx->Umem = (UCHAR *)calloc(RX_RING_SIZE, FRAME_SIZE);
    if (Ctx->Umem == NULL) {
        LOGERR("Failed to allocate UMEM for queue %u", QueueId);
        return E_OUTOFMEMORY;
    }

    //
    // Create the socket.
    //
    XdpStatus = XskCreate(&Ctx->Socket);
    if (FAILED(XdpStatus)) {
        LOGERR("XskCreate failed for queue %u: %x", QueueId, XdpStatus);
        return XdpStatus;
    }

    //
    // Register the UMEM buffer region.
    //
    UmemReg.TotalSize = RX_RING_SIZE * FRAME_SIZE;
    UmemReg.ChunkSize = FRAME_SIZE;
    UmemReg.Address = Ctx->Umem;

    XdpStatus = XskSetSockopt(Ctx->Socket, XSK_SOCKOPT_UMEM_REG, &UmemReg, sizeof(UmemReg));
    if (FAILED(XdpStatus)) {
        LOGERR("XSK_SOCKOPT_UMEM_REG failed for queue %u: %x", QueueId, XdpStatus);
        return XdpStatus;
    }

    //
    // Bind to the interface and queue for RX only.
    //
    XdpStatus = XskBind(Ctx->Socket, IfIndex, QueueId, XSK_BIND_FLAG_RX);
    if (FAILED(XdpStatus)) {
        LOGERR("XskBind failed for queue %u: %x", QueueId, XdpStatus);
        return XdpStatus;
    }

    //
    // Configure RX and fill ring sizes.
    //
    XdpStatus = XskSetSockopt(Ctx->Socket, XSK_SOCKOPT_RX_RING_SIZE, &RingSize, sizeof(RingSize));
    if (FAILED(XdpStatus)) {
        LOGERR("XSK_SOCKOPT_RX_RING_SIZE failed for queue %u: %x", QueueId, XdpStatus);
        return XdpStatus;
    }

    XdpStatus = XskSetSockopt(Ctx->Socket, XSK_SOCKOPT_RX_FILL_RING_SIZE, &RingSize, sizeof(RingSize));
    if (FAILED(XdpStatus)) {
        LOGERR("XSK_SOCKOPT_RX_FILL_RING_SIZE failed for queue %u: %x", QueueId, XdpStatus);
        return XdpStatus;
    }

    //
    // Activate the socket so rings become available.
    //
    XdpStatus = XskActivate(Ctx->Socket, XSK_ACTIVATE_FLAG_NONE);
    if (FAILED(XdpStatus)) {
        LOGERR("XskActivate failed for queue %u: %x", QueueId, XdpStatus);
        return XdpStatus;
    }

    //
    // Retrieve ring info and initialize helper rings.
    //
    OptionLength = sizeof(RingInfo);
    XdpStatus = XskGetSockopt(Ctx->Socket, XSK_SOCKOPT_RING_INFO, &RingInfo, &OptionLength);
    if (FAILED(XdpStatus)) {
        LOGERR("XSK_SOCKOPT_RING_INFO failed for queue %u: %x", QueueId, XdpStatus);
        return XdpStatus;
    }

    XskRingInitialize(&Ctx->RxRing, &RingInfo.Rx);
    XskRingInitialize(&Ctx->RxFillRing, &RingInfo.Fill);

    //
    // Pre-fill the fill ring with all frame descriptors so the socket is
    // immediately ready to receive.
    //
    UINT32 FillIndex;
    XskRingProducerReserve(&Ctx->RxFillRing, RX_RING_SIZE, &FillIndex);
    for (UINT32 j = 0; j < RX_RING_SIZE; j++) {
        *(UINT64 *)XskRingGetElement(&Ctx->RxFillRing, FillIndex + j) =
            (UINT64)j * FRAME_SIZE;
    }
    XskRingProducerSubmit(&Ctx->RxFillRing, RX_RING_SIZE);

    return S_OK;
}

static
VOID
PrintStats(
    _In_ QUEUE_CONTEXT *Queues,
    _In_ UINT32 Count
    )
{
    UINT64 TotalPackets = 0;
    UINT64 TotalBytes = 0;
    UINT64 TotalDrops = 0;

    printf("\n--- RX Stats ---\n");
    for (UINT32 i = 0; i < Count; i++) {
        printf("  Queue %2u: %8llu pkts  %12llu bytes  %8llu drops\n",
            i, Queues[i].PacketsReceived, Queues[i].BytesReceived, Queues[i].PacketsDropped);
        TotalPackets += Queues[i].PacketsReceived;
        TotalBytes += Queues[i].BytesReceived;
        TotalDrops += Queues[i].PacketsDropped;
    }
    printf("  Total:    %8llu pkts  %12llu bytes  %8llu drops\n", TotalPackets, TotalBytes, TotalDrops);
    printf("----------------\n");
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
    QUEUE_CONTEXT *Queues = NULL;
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
    // Allocate per-queue context.
    //
    Queues = (QUEUE_CONTEXT *)calloc(QueueCount, sizeof(QUEUE_CONTEXT));
    if (Queues == NULL) {
        LOGERR("Failed to allocate queue context array");
        return 1;
    }

    //
    // Create, configure, and activate an XSK socket for each queue, then
    // insert into the map.
    //
    for (UINT32 i = 0; i < QueueCount; i++) {
        XdpStatus = SetupQueueSocket(i, &Queues[i]);
        if (FAILED(XdpStatus)) {
            return 1;
        }

        XdpStatus = XdpMapInsert(XskMap, &i, &Queues[i].Socket);
        if (FAILED(XdpStatus)) {
            LOGERR("XdpMapInsert failed for queue %u: %x", i, XdpStatus);
            return 1;
        }

        printf("Queue %u: XSK created, bound, activated, and inserted into XSKMAP\n", i);
    }

    //
    // Create the XDP rule using the XSKMAP.
    //
    XDP_RULE Rule;
    ZeroMemory(&Rule, sizeof(Rule));

    if (UseUdpMatch) {
        Rule.Match = XDP_MATCH_UDP_DST;
        Rule.Pattern.Port = UdpDstPort;
    } else if (UseIcmpMatch) {
        Rule.Match = XDP_MATCH_IP_NEXT_HEADER;
        Rule.Pattern.NextHeader = 1; // IPPROTO_ICMP
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
        "XDP program created. Redirecting %s traffic across %u queues.\n",
        UseUdpMatch ? "matching UDP" : UseIcmpMatch ? "ICMPv4" : "all",
        QueueCount);

    //
    // Poll all queues for received packets. Print stats every second.
    //
    printf("Receiving... (Ctrl+C to stop)\n");

    ULONGLONG StartTick = GetTickCount64();
    ULONGLONG LastPrintTick = StartTick;

    for (;;) {
        //
        // Check timeout.
        //
        ULONGLONG Now = GetTickCount64();
        if (TimeoutSeconds != 0 && (Now - StartTick) >= TimeoutSeconds * 1000) {
            break;
        }

        //
        // Print stats once per second.
        //
        if (Now - LastPrintTick >= 1000) {
            PrintStats(Queues, QueueCount);
            LastPrintTick = Now;
        }

        //
        // Drain RX from each queue.
        //
        for (UINT32 q = 0; q < QueueCount; q++) {
            QUEUE_CONTEXT *Ctx = &Queues[q];
            UINT32 RxIndex;
            UINT32 Available = XskRingConsumerReserve(&Ctx->RxRing, RX_RING_SIZE, &RxIndex);

            if (Available > 0) {
                //
                // Reserve fill ring slots first so we only consume
                // what we can recycle back to the fill ring.
                //
                UINT32 FillIndex;
                UINT32 Filled = XskRingProducerReserve(&Ctx->RxFillRing, Available, &FillIndex);

                for (UINT32 j = 0; j < Filled; j++) {
                    XSK_BUFFER_DESCRIPTOR *Desc =
                        (XSK_BUFFER_DESCRIPTOR *)XskRingGetElement(&Ctx->RxRing, RxIndex + j);
                    Ctx->PacketsReceived++;
                    Ctx->BytesReceived += Desc->Length;

                    //
                    // Re-post the same UMEM offsets. The order doesn't matter;
                    // just cycle through the buffer pool.
                    //
                    *(UINT64 *)XskRingGetElement(&Ctx->RxFillRing, FillIndex + j) =
                        (UINT64)((RxIndex + j) % RX_RING_SIZE) * FRAME_SIZE;
                }

                Ctx->PacketsDropped += (Available - Filled);
                XskRingConsumerRelease(&Ctx->RxRing, Filled);
                XskRingProducerSubmit(&Ctx->RxFillRing, Filled);
            }
        }
    }

    PrintStats(Queues, QueueCount);

    //
    // Clean up.
    //
    CloseHandle(Program);
    CloseHandle(XskMap);
    for (UINT32 i = 0; i < QueueCount; i++) {
        if (Queues[i].Socket != NULL) {
            CloseHandle(Queues[i].Socket);
        }
        free(Queues[i].Umem);
    }
    free(Queues);

    return 0;
}
