//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <afxdp_helper.h>
#include <afxdp_experimental.h>
#include <xdpapi.h>

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#define SHALLOW_STR_OF(x) #x
#define STR_OF(x) SHALLOW_STR_OF(x)

#define ALIGN_DOWN_BY(length, alignment) \
    ((ULONG_PTR)(length)& ~(alignment - 1))
#define ALIGN_UP_BY(length, alignment) \
    (ALIGN_DOWN_BY(((ULONG_PTR)(length)+alignment - 1), alignment))

#define STRUCT_FIELD_OFFSET(structPtr, field) \
    ((UCHAR *)&(structPtr)->field - (UCHAR *)(structPtr))

#define DEFAULT_UMEM_SIZE 65536
#define DEFAULT_UMEM_CHUNK_SIZE 4096
#define DEFAULT_UMEM_HEADROOM 0
#define DEFAULT_IO_BATCH 1
#define DEFAULT_NODE_AFFINITY -1
#define DEFAULT_GROUP -1
#define DEFAULT_IDEAL_CPU -1
#define DEFAULT_CPU_AFFINITY 0
#define DEFAULT_UDP_DEST_PORT 0
#define DEFAULT_DURATION ULONG_MAX
#define DEFAULT_TX_IO_SIZE 64
#define DEFAULT_LAT_COUNT 10000000
#define DEFAULT_YIELD_COUNT 0

CHAR *HELP =
"xskbench.exe <rx|tx|fwd|lat> -i <ifindex> [OPTIONS] <-t THREAD_PARAMS> [-t THREAD_PARAMS...] \n"
"\n"
"THREAD_PARAMS: \n"
"   -q <QUEUE_PARAMS> [-q QUEUE_PARAMS...] \n"
"   -w                 Wait for IO completion\n"
"                      Default: off (busy loop IO mode)\n"
"   -na <nodenumber>   The NUMA node affinity. -1 is any node\n"
"                      Default: " STR_OF(DEFAULT_NODE_AFFINITY) "\n"
"   -group <groupid>   The processor group. -1 is any group\n"
"                      Must be specified alongside -ca\n"
"                      Default: " STR_OF(DEFAULT_GROUP) "\n"
"   -ci <cpuindex>     The ideal CPU. -1 is any CPU\n"
"                      Default: " STR_OF(DEFAULT_IDEAL_CPU) "\n"
"   -ca <cpumask>      The CPU affinity mask. 0 is any CPU\n"
"                      Must be specified alongside -group\n"
"                      Default: " STR_OF(DEFAULT_CPU_AFFINITY) "\n"
"   -yield <count>     The number of yield instructions to execute after the\n"
"                      thread performs no work.\n"
"                      Default: " STR_OF(DEFAULT_YIELD_COUNT) "\n"
"\n"
"QUEUE_PARAMS: \n"
"   -id <queueid>      Required. The queue ID.\n"
"   -ring_size <size>  The ring size (in number of descriptors) of all AF_XDP rings\n"
"                      Default: <umemsize> / <umemchunksize>\n"
"   -u <umemsize>      The total size (in bytes) of the UMEM\n"
"                      Default: " STR_OF(DEFAULT_UMEM_SIZE) "\n"
"   -c <umemchunksize> The size (in bytes) of UMEM chunks\n"
"                      Default: " STR_OF(DEFAULT_UMEM_CHUNK_SIZE) "\n"
"   -h <headroom>      The size (in bytes) of UMEM chunk headroom\n"
"                      Default: " STR_OF(DEFAULT_UMEM_HEADROOM) "\n"
"   -txio <txiosize>   The size (in bytes) of each IO in tx mode\n"
"                      Default: " STR_OF(DEFAULT_TX_IO_SIZE) "\n"
"   -b <iobatchsize>   The number of buffers to submit for IO at once\n"
"                      Default: " STR_OF(DEFAULT_IO_BATCH) "\n"
"   -ignore_needpoke   Ignore the NEED_POKE optimization mechanism\n"
"                      Default: off (Use NEED_POKE)\n"
"   -poll <mode>       The preferred socket polling mode:\n"
"                      - system:  The system default polling mode\n"
"                      - busy:    The system aggressively polls\n"
"                      - socket:  The socket polls\n"
"                      Default: system\n"
"   -xdp_mode <mode>   The XDP interface provider:\n"
"                      - system:  The system determines the ideal XDP provider\n"
"                      - generic: A generic XDP interface provider\n"
"                      - native:  A native XDP interface provider\n"
"                      Default: system\n"
"   -s                 Periodic socket statistics output\n"
"                      Default: off\n"
"   -rx_inject         Inject TX and FWD frames onto the local RX path\n"
"                      Default: off\n"
"   -tx_inspect        Inspect RX and FWD frames from the local TX path\n"
"                      Default: off\n"
"   -tx_pattern        Pattern for the leading bytes of TX, in hexadecimal.\n"
"                      The pktcmd.exe tool outputs hexadecimal headers. Any\n"
"                      trailing bytes in the XSK buffer are set to zero\n"
"                      Default: \"\"\n"
"   -lat_count         Number of latency samples to collect\n"
"                      Default: " STR_OF(DEFAULT_LAT_COUNT) "\n"

"\n"
"OPTIONS: \n"
"   -d                 Duration of execution in seconds\n"
"                      Default: infinite\n"
"   -v                 Verbose logging\n"
"                      Default: off\n"
"   -p <udpPort>       The UDP destination port, or 0 for all traffic.\n"
"                      Default: " STR_OF(DEFAULT_UDP_DEST_PORT) "\n"
"   -lp                Use large pages. Requires privileged account.\n"
"                      Default: off\n"
"\n"
"Examples\n"
"   xskbench.exe rx -i 6 -t -q -id 0\n"
"   xskbench.exe rx -i 6 -t -ca 0x2 -q -id 0 -t -ca 0x4 -q -id 1\n"
"   xskbench.exe tx -i 6 -t -q -id 0 -q -id 1\n"
"   xskbench.exe fwd -i 6 -t -q -id 0 -y\n"
"   xskbench.exe lat -i 6 -t -q -id 0 -ring_size 8\n"
;

#define printf_error(...) \
    fprintf(stderr, __VA_ARGS__)

#define printf_verbose(format, ...) \
    if (verbose) { LARGE_INTEGER Qpc; QueryPerformanceCounter(&Qpc); printf("Qpc=%llu " format, Qpc.QuadPart, __VA_ARGS__); }

#define ABORT(...) \
    printf_error(__VA_ARGS__); exit(1)

#define ASSERT_FRE(expr) \
    if (!(expr)) { ABORT("(%s) failed line %d\n", #expr, __LINE__);}

#if DBG
#define VERIFY(expr) assert(expr)
#else
#define VERIFY(expr) (expr)
#endif

#define Usage() PrintUsage(__LINE__)

#define WAIT_DRIVER_TIMEOUT_MS 1050
#define STATS_ARRAY_SIZE 60

typedef enum {
    ModeRx,
    ModeTx,
    ModeFwd,
    ModeLat,
} MODE;

typedef enum {
    XdpModeSystem,
    XdpModeGeneric,
    XdpModeNative,
} XDP_MODE;

typedef struct {
    INT queueId;
    HANDLE sock;
    HANDLE rxProgram;
    XDP_MODE xdpMode;
    ULONG umemsize;
    ULONG umemchunksize;
    ULONG umemheadroom;
    ULONG txiosize;
    ULONG iobatchsize;
    UINT32 ringsize;
    UCHAR *txPattern;
    UINT32 txPatternLength;
    INT64 *latSamples;
    UINT32 latSamplesCount;
    UINT32 latIndex;
    XSK_POLL_MODE pollMode;

    struct {
        BOOLEAN periodicStats : 1;
        BOOLEAN rx : 1;
        BOOLEAN tx : 1;
        BOOLEAN optimizePoking: 1;
        BOOLEAN rxInject : 1;
        BOOLEAN txInspect : 1;
    } flags;

    double statsArray[STATS_ARRAY_SIZE];
    ULONG currStatsArrayIdx;

    ULONGLONG lastTick;
    ULONGLONG packetCount;
    ULONGLONG lastPacketCount;
    ULONGLONG lastRxDropCount;
    ULONGLONG pokesRequestedCount;
    ULONGLONG lastPokesRequestedCount;
    ULONGLONG pokesPerformedCount;
    ULONGLONG lastPokesPerformedCount;

    XSK_RING rxRing;
    XSK_RING txRing;
    XSK_RING fillRing;
    XSK_RING compRing;
    XSK_RING freeRing;
    XSK_UMEM_REG umemReg;
} MY_QUEUE;

typedef struct {
    HANDLE threadHandle;
    HANDLE readyEvent;
    LONG nodeAffinity;
    LONG group;
    LONG idealCpu;
    UINT32 yieldCount;
    DWORD_PTR cpuAffinity;
    BOOLEAN wait;

    UINT32 queueCount;
    MY_QUEUE *queues;
} MY_THREAD;

CONST XDP_API_TABLE *XdpApi;
INT ifindex = -1;
UINT16 udpDestPort = DEFAULT_UDP_DEST_PORT;
ULONG duration = DEFAULT_DURATION;
BOOLEAN verbose = FALSE;
BOOLEAN done = FALSE;
BOOLEAN largePages = FALSE;
MODE mode;
CHAR *modestr;
HANDLE periodicStatsEvent;

UINT32
RingPairReserve(
    _In_ XSK_RING *ConsumerRing,
    _Out_ UINT32 *ConsumerIndex,
    _In_ XSK_RING *ProducerRing,
    _Out_ UINT32 *ProducerIndex,
    _In_ UINT32 MaxCount
    )
{
    MaxCount = XskRingConsumerReserve(ConsumerRing, MaxCount, ConsumerIndex);
    MaxCount = XskRingProducerReserve(ProducerRing, MaxCount, ProducerIndex);
    return MaxCount;
}

VOID
PrintRing(
    CHAR* Name,
    XSK_RING_INFO RingInfo
    )
{
    if (RingInfo.Size != 0) {
        printf_verbose(
            "%s\tring:\n\tva=0x%p\n\tsize=%d\n\tdescriptorsOff=%d\n\t"
            "producerIndexOff=%d(%lu)\n\tconsumerIndexOff=%d(%lu)\n\t"
            "flagsOff=%d(%lu)\n\telementStride=%d\n",
            Name, RingInfo.Ring, RingInfo.Size, RingInfo.DescriptorsOffset,
            RingInfo.ProducerIndexOffset,
            *(UINT32*)(RingInfo.Ring + RingInfo.ProducerIndexOffset),
            RingInfo.ConsumerIndexOffset,
            *(UINT32*)(RingInfo.Ring + RingInfo.ConsumerIndexOffset),
            RingInfo.FlagsOffset,
            *(UINT32*)(RingInfo.Ring + RingInfo.FlagsOffset),
            RingInfo.ElementStride);
    }
}

VOID
PrintRingInfo(
    XSK_RING_INFO_SET InfoSet
    )
{
    PrintRing("rx", InfoSet.Rx);
    PrintRing("tx", InfoSet.Tx);
    PrintRing("fill", InfoSet.Fill);
    PrintRing("comp", InfoSet.Completion);
}

VOID
AttachXdpProgram(
    MY_QUEUE *Queue
    )
{
    XDP_RULE rule = {0};
    UINT32 flags = 0;
    XDP_HOOK_ID hookId;
    UINT32 hookSize = sizeof(hookId);
    HRESULT res;

    if (!Queue->flags.rx) {
        return;
    }

    rule.Match = udpDestPort == 0 ? XDP_MATCH_ALL : XDP_MATCH_UDP_DST;
    rule.Pattern.Port = _byteswap_ushort(udpDestPort);
    rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    rule.Redirect.Target = Queue->sock;

    if (Queue->xdpMode == XdpModeGeneric) {
        flags |= XDP_CREATE_PROGRAM_FLAG_GENERIC;
    } else if (Queue->xdpMode == XdpModeNative) {
        flags |= XDP_CREATE_PROGRAM_FLAG_NATIVE;
    }

    res = XdpApi->XskGetSockopt(Queue->sock, XSK_SOCKOPT_RX_HOOK_ID, &hookId, &hookSize);
    ASSERT_FRE(SUCCEEDED(res));
    ASSERT_FRE(hookSize == sizeof(hookId));

    res =
        XdpApi->XdpCreateProgram(
            ifindex, &hookId, Queue->queueId, flags, &rule, 1, &Queue->rxProgram);
    if (FAILED(res)) {
        ABORT("XdpCreateProgram failed: %d\n", res);
    }
}

VOID
EnableLargePages(
    VOID
    )
{
    HANDLE Token = NULL;
    TOKEN_PRIVILEGES TokenPrivileges;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token)) {
        goto Failure;
    }

    TokenPrivileges.PrivilegeCount = 1;
    TokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &TokenPrivileges.Privileges[0].Luid)) {
        goto Failure;
    }
    if (!AdjustTokenPrivileges(Token, FALSE, &TokenPrivileges, 0, NULL, 0)) {
        goto Failure;
    }
    if (GetLastError() != ERROR_SUCCESS) {
        goto Failure;
    }

    CloseHandle(Token);

    return;

Failure:

    ABORT("Failed to acquire large page privileges. See \"Assigning Privileges to an Account\"\n");
}

UCHAR
HexToBin(
    _In_ CHAR Char
    )
{
    Char = (CHAR)tolower(Char);

    if (Char >= '0' && Char <= '9') {
        return (UCHAR)(Char - '0');
    }

    if (Char >= 'a' && Char <= 'f') {
        return (UCHAR)(10 + Char - 'a');
    }

    ASSERT_FRE(!"Invalid hex");
    return 0;
}

VOID
GetDescriptorPattern(
    _Inout_ UCHAR *Buffer,
    _In_ UINT32 BufferSize,
    _In_opt_z_ CONST CHAR *Hex
    )
{
    while (Hex != NULL && *Hex != '\0') {
        ASSERT_FRE(BufferSize > 0);

        *Buffer = HexToBin(*Hex++);
        *Buffer <<= 4;

        ASSERT_FRE(*Hex != '\0');
        *Buffer |= HexToBin(*Hex++);

        Buffer++;
        BufferSize--;
    }
}

VOID
SetupSock(
    INT IfIndex,
    MY_QUEUE *Queue
    )
{
    HRESULT res;
    UINT32 bindFlags = 0;

    printf_verbose("creating sock\n");
    res = XdpApi->XskCreate(&Queue->sock);
    if (res != S_OK) {
        ABORT("err: XskCreate returned %d\n", res);
    }

    printf_verbose("XDP_UMEM_REG\n");

    Queue->umemReg.ChunkSize = Queue->umemchunksize;
    Queue->umemReg.Headroom = Queue->umemheadroom;
    Queue->umemReg.TotalSize = Queue->umemsize;

    if (largePages) {
        //
        // The memory subsystem requires allocations and mappings be aligned to
        // the large page size. XDP ignores the final chunk, if truncated.
        //
        Queue->umemReg.TotalSize = ALIGN_UP_BY(Queue->umemReg.TotalSize, GetLargePageMinimum());
    }

    Queue->umemReg.Address =
        VirtualAlloc(
            NULL, Queue->umemReg.TotalSize,
            (largePages ? MEM_LARGE_PAGES : 0) | MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ASSERT_FRE(Queue->umemReg.Address != NULL);

    res =
        XdpApi->XskSetSockopt(
            Queue->sock, XSK_SOCKOPT_UMEM_REG, &Queue->umemReg,
            sizeof(Queue->umemReg));
    ASSERT_FRE(res == S_OK);

    printf_verbose("configuring fill ring with size %d\n", Queue->ringsize);
    res =
        XdpApi->XskSetSockopt(
            Queue->sock, XSK_SOCKOPT_RX_FILL_RING_SIZE, &Queue->ringsize,
            sizeof(Queue->ringsize));
    ASSERT_FRE(res == S_OK);

    printf_verbose("configuring completion ring with size %d\n", Queue->ringsize);
    res =
        XdpApi->XskSetSockopt(
            Queue->sock, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &Queue->ringsize,
            sizeof(Queue->ringsize));
    ASSERT_FRE(res == S_OK);

    if (Queue->flags.rx) {
        printf_verbose("configuring rx ring with size %d\n", Queue->ringsize);
        res =
            XdpApi->XskSetSockopt(
                Queue->sock, XSK_SOCKOPT_RX_RING_SIZE, &Queue->ringsize,
                sizeof(Queue->ringsize));
        ASSERT_FRE(res == S_OK);
        bindFlags |= XSK_BIND_FLAG_RX;
    }
    if (Queue->flags.tx) {
        printf_verbose("configuring tx ring with size %d\n", Queue->ringsize);
        res =
            XdpApi->XskSetSockopt(
                Queue->sock, XSK_SOCKOPT_TX_RING_SIZE, &Queue->ringsize,
                sizeof(Queue->ringsize));
        ASSERT_FRE(res == S_OK);
        bindFlags |= XSK_BIND_FLAG_TX;
    }

    if (Queue->xdpMode == XdpModeGeneric) {
        bindFlags |= XSK_BIND_FLAG_GENERIC;
    } else if (Queue->xdpMode == XdpModeNative) {
        bindFlags |= XSK_BIND_FLAG_NATIVE;
    }

    if (Queue->flags.rxInject) {
        XDP_HOOK_ID hookId = {0};
        hookId.Layer = XDP_HOOK_L2;
        hookId.Direction = XDP_HOOK_RX;
        hookId.SubLayer = XDP_HOOK_INJECT;

        printf_verbose("configuring tx inject to rx\n");
        res = XdpApi->XskSetSockopt(Queue->sock, XSK_SOCKOPT_TX_HOOK_ID, &hookId, sizeof(hookId));
        ASSERT_FRE(res == S_OK);
    }

    if (Queue->flags.txInspect) {
        XDP_HOOK_ID hookId = {0};
        hookId.Layer = XDP_HOOK_L2;
        hookId.Direction = XDP_HOOK_TX;
        hookId.SubLayer = XDP_HOOK_INSPECT;

        printf_verbose("configuring rx from tx inspect\n");
        res = XdpApi->XskSetSockopt(Queue->sock, XSK_SOCKOPT_RX_HOOK_ID, &hookId, sizeof(hookId));
        ASSERT_FRE(res == S_OK);
    }

    printf_verbose(
        "binding sock to ifindex %d queueId %d flags 0x%x\n", IfIndex, Queue->queueId, bindFlags);
    res = XdpApi->XskBind(Queue->sock, IfIndex, Queue->queueId, bindFlags);
    ASSERT_FRE(res == S_OK);

    printf_verbose("activating sock\n");
    res = XdpApi->XskActivate(Queue->sock, 0);
    ASSERT_FRE(res == S_OK);

    printf_verbose("XSK_SOCKOPT_RING_INFO\n");
    XSK_RING_INFO_SET infoSet = { 0 };
    UINT32 ringInfoSize = sizeof(infoSet);
    res = XdpApi->XskGetSockopt(Queue->sock, XSK_SOCKOPT_RING_INFO, &infoSet, &ringInfoSize);
    ASSERT_FRE(res == S_OK);
    ASSERT_FRE(ringInfoSize == sizeof(infoSet));
    PrintRingInfo(infoSet);

    XskRingInitialize(&Queue->fillRing, &infoSet.Fill);
    XskRingInitialize(&Queue->compRing, &infoSet.Completion);

    if (Queue->flags.rx) {
        XskRingInitialize(&Queue->rxRing, &infoSet.Rx);
    }
    if (Queue->flags.tx) {
        XskRingInitialize(&Queue->txRing, &infoSet.Tx);
    }

    res =
        XdpApi->XskSetSockopt(
            Queue->sock, XSK_SOCKOPT_POLL_MODE, &Queue->pollMode, sizeof(Queue->pollMode));
    ASSERT_FRE(res == S_OK);

    //
    // Free ring starts off with all UMEM descriptors.
    //
    UINT32 numDescriptors = Queue->umemsize / Queue->umemchunksize;
    struct {
        UINT32 Producer;
        UINT32 Consumer;
        UINT32 Flags;
        UINT64 Descriptors[0];
    } *FreeRingLayout;

    FreeRingLayout =
        calloc(1, sizeof(*FreeRingLayout) + numDescriptors * sizeof(*FreeRingLayout->Descriptors));
    ASSERT_FRE(FreeRingLayout != NULL);

    XSK_RING_INFO freeRingInfo = {0};
    freeRingInfo.Ring = (BYTE *)FreeRingLayout;
    freeRingInfo.ProducerIndexOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Producer);
    freeRingInfo.ConsumerIndexOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Consumer);
    freeRingInfo.FlagsOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Flags);
    freeRingInfo.DescriptorsOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Descriptors[0]);
    freeRingInfo.Size = numDescriptors;
    freeRingInfo.ElementStride = sizeof(*FreeRingLayout->Descriptors);
    XskRingInitialize(&Queue->freeRing, &freeRingInfo);
    PrintRing("free", freeRingInfo);

    UINT64 desc = 0;
    for (UINT32 i = 0; i < numDescriptors; i++) {
        UINT64 *Descriptor = XskRingGetElement(&Queue->freeRing, i);
        *Descriptor = desc;

        if (mode == ModeTx || mode == ModeLat) {
            memcpy(
                (UCHAR *)Queue->umemReg.Address + desc + Queue->umemheadroom, Queue->txPattern,
                Queue->txPatternLength);
        }

        desc += Queue->umemchunksize;
    }
    XskRingProducerSubmit(&Queue->freeRing, numDescriptors);

    AttachXdpProgram(Queue);
}

VOID
ProcessPeriodicStats(
    MY_QUEUE *Queue
    )
{
    UINT64 currentTick = GetTickCount64();
    UINT64 tickDiff = currentTick - Queue->lastTick;
    UINT64 packetCount;
    UINT64 packetDiff;
    double kpps;

    if (tickDiff == 0) {
        return;
    }

    packetCount = Queue->packetCount;
    packetDiff = packetCount - Queue->lastPacketCount;
    kpps = (packetDiff) ? (double)packetDiff / tickDiff : 0;

    if (Queue->flags.periodicStats) {
        XSK_STATISTICS stats;
        UINT32 optSize = sizeof(stats);
        ULONGLONG pokesRequested = Queue->pokesRequestedCount;
        ULONGLONG pokesPerformed = Queue->pokesPerformedCount;
        ULONGLONG pokesRequestedDiff;
        ULONGLONG pokesPerformedDiff;
        ULONGLONG pokesAvoidedPercentage;
        ULONGLONG rxDropDiff;
        double rxDropKpps;

        if (pokesPerformed > pokesRequested) {
            //
            // Since these statistics aren't protected by synchronization, it's
            // possible instruction reordering resulted in (pokesPerformed >
            // pokesRequested). We know pokesPerformed <= pokesRequested, so
            // correct this.
            //
            pokesRequested = pokesPerformed;
        }

        pokesRequestedDiff = pokesRequested - Queue->lastPokesRequestedCount;
        pokesPerformedDiff = pokesPerformed - Queue->lastPokesPerformedCount;

        if (pokesRequestedDiff == 0) {
            pokesAvoidedPercentage = 0;
        } else {
            pokesAvoidedPercentage =
                (pokesRequestedDiff - pokesPerformedDiff) * 100 / pokesRequestedDiff;
        }

        HRESULT res =
            XdpApi->XskGetSockopt(Queue->sock, XSK_SOCKOPT_STATISTICS, &stats, &optSize);
        ASSERT_FRE(res == S_OK);
        ASSERT_FRE(optSize == sizeof(stats));

        rxDropDiff = stats.RxDropped - Queue->lastRxDropCount;
        rxDropKpps = rxDropDiff ? (double)rxDropDiff / tickDiff : 0;
        Queue->lastRxDropCount = stats.RxDropped;

        printf("%s[%d]: %9.3f kpps %9.3f rxDropKpps rxDrop:%llu rxTrunc:%llu "
            "rxBadDesc:%llu txBadDesc:%llu pokesAvoided:%llu%%\n",
            modestr, Queue->queueId, kpps, rxDropKpps, stats.RxDropped, stats.RxTruncated,
            stats.RxInvalidDescriptors, stats.TxInvalidDescriptors,
            pokesAvoidedPercentage);

        Queue->lastPokesRequestedCount = pokesRequested;
        Queue->lastPokesPerformedCount = pokesPerformed;
    }

    Queue->statsArray[Queue->currStatsArrayIdx++ % STATS_ARRAY_SIZE] = kpps;
    Queue->lastPacketCount = packetCount;
    Queue->lastTick = currentTick;
}

INT
LatCmp(
    CONST VOID *A,
    CONST VOID *B
    )
{
    CONST UINT64 *a = A;
    CONST UINT64 *b = B;
    return (*a > *b) - (*a < *b);
}

INT64
QpcToUs64(
    INT64 Qpc,
    INT64 QpcFrequency
    )
{
    //
    // Multiply by a big number (1000000, to convert seconds to microseconds)
    // and divide by a big number (QpcFrequency, to convert counts to secs).
    //
    // Avoid overflow with separate multiplication/division of the high and low
    // bits.
    //
    // Taken from QuicTimePlatToUs64 (https://github.com/microsoft/msquic).
    //
    UINT64 High = (Qpc >> 32) * 1000000;
    UINT64 Low = (Qpc & 0xFFFFFFFF) * 1000000;
    return
        ((High / QpcFrequency) << 32) +
        ((Low + ((High % QpcFrequency) << 32)) / QpcFrequency);
}

VOID
PrintFinalLatStats(
    MY_QUEUE *Queue
    )
{
    LARGE_INTEGER FreqQpc;
    VERIFY(QueryPerformanceFrequency(&FreqQpc));

    qsort(Queue->latSamples, Queue->latIndex, sizeof(*Queue->latSamples), LatCmp);

    for (UINT32 i = 0; i < Queue->latIndex; i++) {
        Queue->latSamples[i] = QpcToUs64(Queue->latSamples[i], FreqQpc.QuadPart);
    }

    printf(
        "%-3s[%d]: min=%llu P50=%llu P90=%llu P99=%llu P99.9=%llu P99.99=%llu P99.999=%llu P99.9999=%llu us rtt\n",
        modestr, Queue->queueId,
        Queue->latSamples[0],
        Queue->latSamples[(UINT32)(Queue->latIndex * 0.5)],
        Queue->latSamples[(UINT32)(Queue->latIndex * 0.9)],
        Queue->latSamples[(UINT32)(Queue->latIndex * 0.99)],
        Queue->latSamples[(UINT32)(Queue->latIndex * 0.999)],
        Queue->latSamples[(UINT32)(Queue->latIndex * 0.9999)],
        Queue->latSamples[(UINT32)(Queue->latIndex * 0.99999)],
        Queue->latSamples[(UINT32)(Queue->latIndex * 0.999999)]);
}

VOID
PrintFinalStats(
    MY_QUEUE *Queue
    )
{
    ULONG numEntries = min(Queue->currStatsArrayIdx, STATS_ARRAY_SIZE);
    ULONG numEntriesIgnored = 0;
    double min = 99999999;
    double max = 0;
    double sum = 0;
    double avg = 0;
    double stdDev = 0;

    if (numEntries < 4) {
        //
        // We ignore first and last data points and standard deviation
        // calculation needs at least 2 data points.
        //
        printf_error(
            "%-3s[%d] Not enough data points collected for a statistical analysis\n",
            modestr, Queue->queueId);
        return;
    }

    //
    // Scrub the statistics by ignoring the first and last entries.
    //
    if (Queue->currStatsArrayIdx <= STATS_ARRAY_SIZE) {
        Queue->statsArray[0] = 0;
        numEntriesIgnored++;
    }
    Queue->statsArray[(Queue->currStatsArrayIdx - 1) % STATS_ARRAY_SIZE] = 0;
    numEntriesIgnored++;

    //
    // Average, min and max.
    //
    for (ULONG i = 0; i < numEntries; i++) {
        if (Queue->statsArray[i] == 0) {
            continue;
        }

        sum += Queue->statsArray[i];
        min = min(min, Queue->statsArray[i]);
        max = max(max, Queue->statsArray[i]);
    }

    numEntries -= numEntriesIgnored;
    avg = sum / numEntries;

    //
    // Standard deviation.
    //
    for (ULONG i = 0; i < numEntries; i++) {
        if (Queue->statsArray[i] == 0) {
            continue;
        }

        stdDev += pow(Queue->statsArray[i] - avg, 2);
    }

    stdDev = sqrt(stdDev / (numEntries - 1));

    printf("%-3s[%d]: avg=%08.3f stddev=%08.3f min=%08.3f max=%08.3f Kpps\n",
        modestr, Queue->queueId, avg, stdDev, min, max);

    if (mode == ModeLat) {
        PrintFinalLatStats(Queue);
    }
}

VOID
NotifyDriver(
    MY_QUEUE *Queue,
    XSK_NOTIFY_FLAGS DirectionFlags
    )
{
    HRESULT res;
    XSK_NOTIFY_RESULT_FLAGS notifyResult;

    if (Queue->flags.optimizePoking) {
        //
        // Ensure poke flags are read after writing producer/consumer indices.
        //
        MemoryBarrier();

        if ((DirectionFlags & XSK_NOTIFY_FLAG_POKE_RX) && !XskRingProducerNeedPoke(&Queue->fillRing)) {
            DirectionFlags &= ~XSK_NOTIFY_FLAG_POKE_RX;
        }
        if ((DirectionFlags & XSK_NOTIFY_FLAG_POKE_TX) && !XskRingProducerNeedPoke(&Queue->txRing)) {
            DirectionFlags &= ~XSK_NOTIFY_FLAG_POKE_TX;
        }
    }

    Queue->pokesRequestedCount++;

    if (DirectionFlags != 0) {
        Queue->pokesPerformedCount++;
        res =
            XdpApi->XskNotifySocket(
                Queue->sock, DirectionFlags, WAIT_DRIVER_TIMEOUT_MS, &notifyResult);

        if (DirectionFlags & (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX)) {
            ASSERT_FRE(res == S_OK || res == HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        } else {
            ASSERT_FRE(res == S_OK);
            ASSERT_FRE(notifyResult == 0);
        }
    }
}

VOID
WriteFillPackets(
    MY_QUEUE *Queue,
    UINT32 FreeConsumerIndex,
    UINT32 FillProducerIndex,
    UINT32 Count
    )
{
    for (UINT32 i = 0; i < Count; i++) {
        UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, FreeConsumerIndex++);
        UINT64 *fillDesc = XskRingGetElement(&Queue->fillRing, FillProducerIndex++);

        *fillDesc = *freeDesc;
        printf_verbose("Producing FILL entry {address:%llu}}\n", *freeDesc);
    }
}

VOID
ReadRxPackets(
    MY_QUEUE *Queue,
    UINT32 RxConsumerIndex,
    UINT32 FreeProducerIndex,
    UINT32 Count
    )
{
    for (UINT32 i = 0; i < Count; i++) {
        XSK_BUFFER_DESCRIPTOR *rxDesc = XskRingGetElement(&Queue->rxRing, RxConsumerIndex++);
        UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, FreeProducerIndex++);

        *freeDesc = rxDesc->Address.BaseAddress;
        printf_verbose("Consuming RX entry   {address:%llu, offset:%llu, length:%d}\n",
            rxDesc->Address.BaseAddress, rxDesc->Address.Offset, rxDesc->Length);
    }
}

UINT32
ProcessRx(
    MY_QUEUE *Queue,
    BOOLEAN Wait
    )
{
    XSK_NOTIFY_FLAGS notifyFlags = XSK_NOTIFY_FLAG_NONE;
    UINT32 available;
    UINT32 consumerIndex;
    UINT32 producerIndex;
    UINT32 processed = 0;

    available =
        RingPairReserve(
            &Queue->rxRing, &consumerIndex, &Queue->freeRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        ReadRxPackets(Queue, consumerIndex, producerIndex, available);
        XskRingConsumerRelease(&Queue->rxRing, available);
        XskRingProducerSubmit(&Queue->freeRing, available);

        processed += available;
        Queue->packetCount += available;
    }

    available =
        RingPairReserve(
            &Queue->freeRing, &consumerIndex, &Queue->fillRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        WriteFillPackets(Queue, consumerIndex, producerIndex, available);
        XskRingConsumerRelease(&Queue->freeRing, available);
        XskRingProducerSubmit(&Queue->fillRing, available);

        processed += available;
        notifyFlags |= XSK_NOTIFY_FLAG_POKE_RX;
    }

    if (Wait &&
        XskRingConsumerReserve(&Queue->rxRing, 1, &consumerIndex) == 0 &&
        XskRingConsumerReserve(&Queue->freeRing, 1, &consumerIndex) == 0) {
        notifyFlags |= XSK_NOTIFY_FLAG_WAIT_RX;
    }

    if (Queue->pollMode == XSK_POLL_MODE_SOCKET) {
        //
        // If socket poll mode is supported by the program, always enable pokes.
        //
        notifyFlags |= XSK_NOTIFY_FLAG_POKE_RX;
    }

    if (notifyFlags != 0) {
        NotifyDriver(Queue, notifyFlags);
    }

    return processed;
}

VOID
DoRxMode(
    MY_THREAD *Thread
    )
{
    for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
        MY_QUEUE *queue = &Thread->queues[qIndex];

        queue->flags.rx = TRUE;
        SetupSock(ifindex, queue);
        queue->lastTick = GetTickCount64();
    }

    printf("Receiving...\n");
    SetEvent(Thread->readyEvent);

    while (!ReadBooleanNoFence(&done)) {
        BOOLEAN Processed = FALSE;

        for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
            Processed |= !!ProcessRx(&Thread->queues[qIndex], Thread->wait);
        }

        if (!Processed) {
            for (UINT32 i = 0; i < Thread->yieldCount; i++) {
                YieldProcessor();
            }
        }
    }
}

VOID
WriteTxPackets(
    MY_QUEUE *Queue,
    UINT32 FreeConsumerIndex,
    UINT32 TxProducerIndex,
    UINT32 Count
    )
{
    for (UINT32 i = 0; i < Count; i++) {
        UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, FreeConsumerIndex++);
        XSK_BUFFER_DESCRIPTOR *txDesc = XskRingGetElement(&Queue->txRing, TxProducerIndex++);

        txDesc->Address.BaseAddress = *freeDesc;
        assert(Queue->umemReg.Headroom <= MAXUINT16);
        txDesc->Address.Offset = (UINT16)Queue->umemReg.Headroom;
        txDesc->Length = Queue->txiosize;
        //
        // This benchmark does not write data into the TX packet.
        //
        printf_verbose("Producing TX entry {address:%llu, offset:%llu, length:%d}\n",
            txDesc->Address.BaseAddress, txDesc->Address.Offset, txDesc->Length);
    }
}

VOID
ReadCompletionPackets(
    MY_QUEUE *Queue,
    UINT32 CompConsumerIndex,
    UINT32 FreeProducerIndex,
    UINT32 Count
    )
{
    for (UINT32 i = 0; i < Count; i++) {
        UINT64 *compDesc = XskRingGetElement(&Queue->compRing, CompConsumerIndex++);
        UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, FreeProducerIndex++);

        *freeDesc = *compDesc;
        printf_verbose("Consuming COMP entry {address:%llu}\n", *compDesc);
    }
}

UINT32
ProcessTx(
    MY_QUEUE *Queue,
    BOOLEAN Wait
    )
{
    XSK_NOTIFY_FLAGS notifyFlags = XSK_NOTIFY_FLAG_NONE;
    UINT32 available;
    UINT32 consumerIndex;
    UINT32 producerIndex;
    UINT32 processed = 0;

    available =
        RingPairReserve(
            &Queue->compRing, &consumerIndex, &Queue->freeRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        ReadCompletionPackets(Queue, consumerIndex, producerIndex, available);
        XskRingConsumerRelease(&Queue->compRing, available);
        XskRingProducerSubmit(&Queue->freeRing, available);

        processed += available;
        Queue->packetCount += available;

        if (XskRingProducerReserve(&Queue->txRing, MAXUINT32, &producerIndex) !=
                Queue->txRing.Size) {
            notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
        }
    }

    available =
        RingPairReserve(
            &Queue->freeRing, &consumerIndex, &Queue->txRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        WriteTxPackets(Queue, consumerIndex, producerIndex, available);
        XskRingConsumerRelease(&Queue->freeRing, available);
        XskRingProducerSubmit(&Queue->txRing, available);

        processed += available;
        notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
    }

    if (Wait &&
        XskRingConsumerReserve(&Queue->compRing, 1, &consumerIndex) == 0 &&
        XskRingConsumerReserve(&Queue->freeRing, 1, &consumerIndex) == 0) {
        notifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
    }

    if (Queue->pollMode == XSK_POLL_MODE_SOCKET) {
        //
        // If socket poll mode is supported by the program, always enable pokes.
        //
        notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
    }

    if (notifyFlags != 0) {
        NotifyDriver(Queue, notifyFlags);
    }

    return processed;
}

VOID
DoTxMode(
    MY_THREAD *Thread
    )
{
    for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
        MY_QUEUE *queue = &Thread->queues[qIndex];

        queue->flags.tx = TRUE;
        SetupSock(ifindex, queue);
        queue->lastTick = GetTickCount64();
    }

    printf("Sending...\n");
    SetEvent(Thread->readyEvent);

    while (!ReadBooleanNoFence(&done)) {
        BOOLEAN Processed = FALSE;

        for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
            Processed |= ProcessTx(&Thread->queues[qIndex], Thread->wait);
        }

        if (!Processed) {
            for (UINT32 i = 0; i < Thread->yieldCount; i++) {
                YieldProcessor();
            }
        }

    }
}

UINT32
ProcessFwd(
    MY_QUEUE *Queue,
    BOOLEAN Wait
    )
{
    XSK_NOTIFY_FLAGS notifyFlags = XSK_NOTIFY_FLAG_NONE;
    UINT32 available;
    UINT32 consumerIndex;
    UINT32 producerIndex;
    UINT32 processed = 0;

    //
    // Move packets from the RX ring to the TX ring.
    //
    available =
        RingPairReserve(
            &Queue->rxRing, &consumerIndex, &Queue->txRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        for (UINT32 i = 0; i < available; i++) {
            XSK_BUFFER_DESCRIPTOR *rxDesc = XskRingGetElement(&Queue->rxRing, consumerIndex++);
            XSK_BUFFER_DESCRIPTOR *txDesc = XskRingGetElement(&Queue->txRing, producerIndex++);

            printf_verbose("Consuming RX entry   {address:%llu, offset:%llu, length:%d}\n",
                rxDesc->Address.BaseAddress, rxDesc->Address.Offset, rxDesc->Length);

            txDesc->Address = rxDesc->Address;
            txDesc->Length = rxDesc->Length;

            if (Queue->flags.rxInject == Queue->flags.txInspect) {
                //
                // Swap MAC addresses.
                //
                CHAR *ethHdr =
                    (CHAR*)Queue->umemReg.Address + txDesc->Address.BaseAddress +
                        txDesc->Address.Offset;
                CHAR tmp[6];
                memcpy(tmp, ethHdr, sizeof(tmp));
                memcpy(ethHdr, ethHdr + 6, sizeof(tmp));
                memcpy(ethHdr + 6, tmp, sizeof(tmp));
            }

            printf_verbose("Producing TX entry {address:%llu, offset:%llu, length:%d}\n",
                txDesc->Address.BaseAddress, txDesc->Address.Offset, txDesc->Length);
        }

        XskRingConsumerRelease(&Queue->rxRing, available);
        XskRingProducerSubmit(&Queue->txRing, available);

        processed += available;
        notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
    }

    //
    // Move packets from the completion ring to the free ring.
    //
    available =
        RingPairReserve(
            &Queue->compRing, &consumerIndex, &Queue->freeRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        for (UINT32 i = 0; i < available; i++) {
            UINT64 *compDesc = XskRingGetElement(&Queue->compRing, consumerIndex++);
            UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, producerIndex++);

            *freeDesc = *compDesc;

            printf_verbose("Consuming COMP entry {address:%llu}\n", *compDesc);
        }

        XskRingConsumerRelease(&Queue->compRing, available);
        XskRingProducerSubmit(&Queue->freeRing, available);

        processed += available;
        Queue->packetCount += available;

        if (XskRingProducerReserve(&Queue->txRing, MAXUINT32, &producerIndex) !=
                Queue->txRing.Size) {
            notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
        }
    }

    //
    // Move packets from the free ring to the fill ring.
    //
    available =
        RingPairReserve(
            &Queue->freeRing, &consumerIndex, &Queue->fillRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        for (UINT32 i = 0; i < available; i++) {
            UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, consumerIndex++);
            UINT64 *fillDesc = XskRingGetElement(&Queue->fillRing, producerIndex++);

            *fillDesc = *freeDesc;

            printf_verbose("Producing FILL entry {address:%llu}\n", *freeDesc);
        }

        XskRingConsumerRelease(&Queue->freeRing, available);
        XskRingProducerSubmit(&Queue->fillRing, available);

        processed += available;
        notifyFlags |= XSK_NOTIFY_FLAG_POKE_RX;
    }

    if (Wait &&
        XskRingConsumerReserve(&Queue->rxRing, 1, &consumerIndex) == 0 &&
        XskRingConsumerReserve(&Queue->compRing, 1, &consumerIndex) == 0 &&
        XskRingConsumerReserve(&Queue->freeRing, 1, &consumerIndex) == 0) {
        notifyFlags |= (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX);
    }

    if (Queue->pollMode == XSK_POLL_MODE_SOCKET) {
        //
        // If socket poll mode is supported by the program, always enable pokes.
        //
        notifyFlags |= (XSK_NOTIFY_FLAG_POKE_RX | XSK_NOTIFY_FLAG_POKE_TX);
    }

    if (notifyFlags != 0) {
        NotifyDriver(Queue, notifyFlags);
    }

    return processed;
}

VOID
DoFwdMode(
    MY_THREAD *Thread
    )
{
    for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
        MY_QUEUE *queue = &Thread->queues[qIndex];

        queue->flags.rx = TRUE;
        queue->flags.tx = TRUE;
        SetupSock(ifindex, queue);
        queue->lastTick = GetTickCount64();
    }

    printf("Forwarding...\n");
    SetEvent(Thread->readyEvent);

    while (!ReadBooleanNoFence(&done)) {
        BOOLEAN Processed = FALSE;

        for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
            Processed |= !!ProcessFwd(&Thread->queues[qIndex], Thread->wait);
        }

        if (!Processed) {
            for (UINT32 i = 0; i < Thread->yieldCount; i++) {
                YieldProcessor();
            }
        }

    }
}

UINT32
ProcessLat(
    MY_QUEUE *Queue,
    BOOLEAN Wait
    )
{
    XSK_NOTIFY_FLAGS notifyFlags = XSK_NOTIFY_FLAG_NONE;
    UINT32 available;
    UINT32 consumerIndex;
    UINT32 producerIndex;
    UINT32 processed = 0;

    //
    // Move frames from the RX ring to the RX fill ring, recording the timestamp
    // deltas as we go.
    //
    available =
        RingPairReserve(
            &Queue->rxRing, &consumerIndex, &Queue->fillRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        LARGE_INTEGER NowQpc;
        VERIFY(QueryPerformanceCounter(&NowQpc));

        for (UINT32 i = 0; i < available; i++) {
            XSK_BUFFER_DESCRIPTOR *rxDesc = XskRingGetElement(&Queue->rxRing, consumerIndex++);
            UINT64 *fillDesc = XskRingGetElement(&Queue->fillRing, producerIndex++);

            printf_verbose(
                "Consuming RX entry   {address:%llu, offset:%llu, length:%d}\n",
                rxDesc->Address.BaseAddress, rxDesc->Address.Offset,
                rxDesc->Length);

            INT64 UNALIGNED *Timestamp = (INT64 UNALIGNED *)
                ((CHAR*)Queue->umemReg.Address + rxDesc->Address.BaseAddress +
                    rxDesc->Address.Offset + Queue->txPatternLength);

            printf_verbose("latency: %lld\n", NowQpc.QuadPart - *Timestamp);

            if (Queue->latIndex < Queue->latSamplesCount) {
                Queue->latSamples[Queue->latIndex++] = NowQpc.QuadPart - *Timestamp;
            }

            *fillDesc = rxDesc->Address.BaseAddress;

            printf_verbose("Producing FILL entry {address:%llu}\n", *fillDesc);
        }

        XskRingConsumerRelease(&Queue->rxRing, available);
        XskRingProducerSubmit(&Queue->fillRing, available);

        processed += available;
        Queue->packetCount += available;

        notifyFlags |= XSK_NOTIFY_FLAG_POKE_RX;
    }

    //
    // Move frames from the TX completion ring to the free ring.
    //
    available =
        RingPairReserve(
            &Queue->compRing, &consumerIndex, &Queue->freeRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        ReadCompletionPackets(Queue, consumerIndex, producerIndex, available);
        XskRingConsumerRelease(&Queue->compRing, available);
        XskRingProducerSubmit(&Queue->freeRing, available);
        processed += available;
    }

    //
    // Move frames from the free ring to the TX ring, stamping the current time
    // onto each frame.
    //
    available =
        RingPairReserve(
            &Queue->freeRing, &consumerIndex, &Queue->txRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        LARGE_INTEGER NowQpc;
        VERIFY(QueryPerformanceCounter(&NowQpc));

        for (UINT32 i = 0; i < available; i++) {
            UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, consumerIndex++);
            XSK_BUFFER_DESCRIPTOR *txDesc = XskRingGetElement(&Queue->txRing, producerIndex++);

            INT64 UNALIGNED *Timestamp = (INT64 UNALIGNED *)
                ((CHAR*)Queue->umemReg.Address + *freeDesc +
                    Queue->umemReg.Headroom + Queue->txPatternLength);
            *Timestamp = NowQpc.QuadPart;

            txDesc->Address.BaseAddress = *freeDesc;
            assert(Queue->umemReg.Headroom <= MAXUINT16);
            txDesc->Address.Offset = Queue->umemReg.Headroom;
            txDesc->Length = Queue->txiosize;

            printf_verbose(
                "Producing TX entry {address:%llu, offset:%llu, length:%d}\n",
                txDesc->Address.BaseAddress, txDesc->Address.Offset, txDesc->Length);
        }

        XskRingConsumerRelease(&Queue->freeRing, available);
        XskRingProducerSubmit(&Queue->txRing, available);

        processed += available;
        notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
    }

    if (Wait &&
        XskRingConsumerReserve(&Queue->rxRing, 1, &consumerIndex) == 0 &&
        XskRingConsumerReserve(&Queue->compRing, 1, &consumerIndex) == 0 &&
        XskRingConsumerReserve(&Queue->freeRing, 1, &consumerIndex) == 0) {
        notifyFlags |= (XSK_NOTIFY_FLAG_WAIT_RX | XSK_NOTIFY_FLAG_WAIT_TX);
    }

    if (Queue->pollMode == XSK_POLL_MODE_SOCKET) {
        //
        // If socket poll mode is supported by the program, always enable pokes.
        //
        notifyFlags |= (XSK_NOTIFY_FLAG_POKE_RX | XSK_NOTIFY_FLAG_POKE_TX);
    }

    if (notifyFlags != 0) {
        NotifyDriver(Queue, notifyFlags);
    }

    return processed;
}

VOID
DoLatMode(
    MY_THREAD *Thread
    )
{
    for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
        MY_QUEUE *queue = &Thread->queues[qIndex];
        UINT32 consumerIndex;
        UINT32 producerIndex;
        UINT32 available;

        queue->flags.rx = TRUE;
        queue->flags.tx = TRUE;
        SetupSock(ifindex, queue);
        queue->lastTick = GetTickCount64();

        //
        // Fill up the RX fill ring. Once this initial fill is performed, the
        // RX fill ring and RX ring operate in a closed loop.
        //
        available = XskRingProducerReserve(&queue->fillRing, queue->ringsize, &producerIndex);
        ASSERT_FRE(available == queue->ringsize);
        available = XskRingConsumerReserve(&queue->freeRing, queue->ringsize, &consumerIndex);
        ASSERT_FRE(available == queue->ringsize);
        WriteFillPackets(queue, consumerIndex, producerIndex, available);
        XskRingConsumerRelease(&queue->freeRing, available);
        XskRingProducerSubmit(&queue->fillRing, available);
    }

    printf("Probing latency...\n");
    SetEvent(Thread->readyEvent);

    while (!ReadBooleanNoFence(&done)) {
        BOOLEAN Processed = FALSE;

        for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
            Processed |= !!ProcessLat(&Thread->queues[qIndex], Thread->wait);
        }

        if (!Processed) {
            for (UINT32 i = 0; i < Thread->yieldCount; i++) {
                YieldProcessor();
            }
        }
    }
}

VOID
PrintUsage(
    INT Line
    )
{
    printf_error("Line:%d\n", Line);
    ABORT(HELP);
}

VOID
ParseQueueArgs(
    MY_QUEUE *Queue,
    INT argc,
    CHAR **argv
    )
{
    Queue->queueId = -1;
    Queue->xdpMode = XdpModeSystem;
    Queue->umemsize = DEFAULT_UMEM_SIZE;
    Queue->umemchunksize = DEFAULT_UMEM_CHUNK_SIZE;
    Queue->umemheadroom = DEFAULT_UMEM_HEADROOM;
    Queue->iobatchsize = DEFAULT_IO_BATCH;
    Queue->pollMode = XSK_POLL_MODE_DEFAULT;
    Queue->flags.optimizePoking = TRUE;
    Queue->txiosize = DEFAULT_TX_IO_SIZE;
    Queue->latSamplesCount = DEFAULT_LAT_COUNT;

    for (INT i = 0; i < argc; i++) {
        if (!_stricmp(argv[i], "-id")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->queueId = atoi(argv[i]);
        } else if (!strcmp(argv[i], "-ring_size")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->ringsize = atoi(argv[i]);
        } else if (!strcmp(argv[i], "-c")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->umemchunksize = atoi(argv[i]);
        } else if (!_stricmp(argv[i], "-txio")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->txiosize = atoi(argv[i]);
        } else if (!strcmp(argv[i], "-u")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->umemsize = atoi(argv[i]);
        } else if (!strcmp(argv[i], "-b")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->iobatchsize = atoi(argv[i]);
        } else if (!strcmp(argv[i], "-h")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->umemheadroom = atoi(argv[i]);
        } else if (!strcmp(argv[i], "-s")) {
            Queue->flags.periodicStats = TRUE;
        } else if (!_stricmp(argv[i], "-ignore_needpoke")) {
            Queue->flags.optimizePoking = FALSE;
        } else if (!_stricmp(argv[i], "-poll")) {
            if (++i >= argc) {
                Usage();
            }
            if (!_stricmp(argv[i], "system")) {
                Queue->pollMode = XSK_POLL_MODE_DEFAULT;
            } else if (!_stricmp(argv[i], "busy")) {
                Queue->pollMode = XSK_POLL_MODE_BUSY;
            } else if (!_stricmp(argv[i], "socket")) {
                Queue->pollMode = XSK_POLL_MODE_SOCKET;
            } else {
                Usage();
            }
        } else if (!_stricmp(argv[i], "-xdp_mode")) {
            if (++i >= argc) {
                Usage();
            }
            if (!_stricmp(argv[i], "system")) {
                Queue->xdpMode = XdpModeSystem;
            } else if (!_stricmp(argv[i], "generic")) {
                Queue->xdpMode = XdpModeGeneric;
            } else if (!_stricmp(argv[i], "native")) {
                Queue->xdpMode = XdpModeNative;
            } else {
                Usage();
            }
        } else if (!strcmp(argv[i], "-rx_inject")) {
            Queue->flags.rxInject = TRUE;
        } else if (!strcmp(argv[i], "-tx_inspect")) {
            Queue->flags.txInspect = TRUE;
        } else if (!strcmp(argv[i], "-tx_pattern")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->txPatternLength = (UINT32)strlen(argv[i]);
            ASSERT_FRE(Queue->txPatternLength > 0 && Queue->txPatternLength % 2 == 0);
            Queue->txPatternLength /= 2;
            Queue->txPattern = malloc(Queue->txPatternLength);
            ASSERT_FRE(Queue->txPattern != NULL);
            GetDescriptorPattern(Queue->txPattern, Queue->txPatternLength, argv[i]);
        } else if (!strcmp(argv[i], "-lat_count")) {
            if (++i >= argc) {
                Usage();
            }
            Queue->latSamplesCount = atoi(argv[i]);
        } else {
            Usage();
        }
    }

    if (Queue->queueId == -1) {
        Usage();
    }

    if (Queue->ringsize == 0) {
        Queue->ringsize = Queue->umemsize / Queue->umemchunksize;
    }

    ASSERT_FRE(Queue->umemsize >= Queue->umemchunksize);
    ASSERT_FRE(Queue->umemchunksize >= Queue->umemheadroom);
    ASSERT_FRE(Queue->umemchunksize - Queue->umemheadroom >= Queue->txPatternLength);

    if (mode == ModeLat) {
        ASSERT_FRE(
            Queue->umemchunksize - Queue->umemheadroom >= Queue->txPatternLength + sizeof(UINT64));

        Queue->latSamples = malloc(Queue->latSamplesCount * sizeof(*Queue->latSamples));
        ASSERT_FRE(Queue->latSamples != NULL);
        ZeroMemory(Queue->latSamples, Queue->latSamplesCount * sizeof(*Queue->latSamples));
    }
}

VOID
ParseThreadArgs(
    MY_THREAD *Thread,
    INT argc,
    CHAR **argv
    )
{
    BOOLEAN groupSet = FALSE;
    BOOLEAN cpuAffinitySet = FALSE;

    Thread->wait = FALSE;
    Thread->nodeAffinity = DEFAULT_NODE_AFFINITY;
    Thread->idealCpu = DEFAULT_IDEAL_CPU;
    Thread->cpuAffinity = DEFAULT_CPU_AFFINITY;
    Thread->group = DEFAULT_GROUP;
    Thread->yieldCount = DEFAULT_YIELD_COUNT;

    for (INT i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) {
            Thread->queueCount++;
        } else if (!_stricmp(argv[i], "-na")) {
            if (++i >= argc) {
                Usage();
            }
            Thread->nodeAffinity = atoi(argv[i]);
        } else if (!_stricmp(argv[i], "-group")) {
            if (++i >= argc) {
                Usage();
            }
            Thread->group = atoi(argv[i]);
            groupSet = TRUE;
        } else if (!_stricmp(argv[i], "-ci")) {
            if (++i >= argc) {
                Usage();
            }
            Thread->idealCpu = atoi(argv[i]);
        } else if (!_stricmp(argv[i], "-ca")) {
            if (++i >= argc) {
                Usage();
            }
            Thread->cpuAffinity = (DWORD_PTR)_strtoui64(argv[i], NULL, 0);
            cpuAffinitySet = TRUE;
        } else if (!strcmp(argv[i], "-w")) {
            Thread->wait = TRUE;
        } else if (!_stricmp(argv[i], "-yield")) {
            if (++i >= argc) {
                Usage();
            }
            Thread->yieldCount = atoi(argv[i]);
        } else if (Thread->queueCount == 0) {
            Usage();
        }
    }

    if (Thread->queueCount == 0) {
        Usage();
    }

    if (Thread->wait && Thread->queueCount > 1) {
        printf_error("Waiting with multiple sockets per thread is not supported\n");
        Usage();
    }

    if (groupSet != cpuAffinitySet) {
        Usage();
    }

    Thread->queues = calloc(Thread->queueCount, sizeof(*Thread->queues));
    ASSERT_FRE(Thread->queues != NULL);

    INT qStart = -1;
    INT qIndex = 0;
    for (INT i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) {
            if (qStart != -1) {
                ParseQueueArgs(&Thread->queues[qIndex++], i - qStart, &argv[qStart]);
            }
            qStart = i + 1;
        }
    }
    ParseQueueArgs(&Thread->queues[qIndex++], argc - qStart, &argv[qStart]);
}

VOID
ParseArgs(
    MY_THREAD **ThreadsPtr,
    UINT32 *ThreadCountPtr,
    INT argc,
    CHAR **argv
    )
{
    INT i = 1;
    UINT32 threadCount = 0;
    MY_THREAD *threads = NULL;

    if (argc < 4) {
        Usage();
    }

    if (!_stricmp(argv[i], "rx")) {
        mode = ModeRx;
    } else if (!_stricmp(argv[i], "tx")) {
        mode = ModeTx;
    } else if (!_stricmp(argv[i], "fwd")) {
        mode = ModeFwd;
    } else if (!_stricmp(argv[i], "lat")) {
        mode = ModeLat;
    } else {
        Usage();
    }
    modestr = argv[i];
    ++i;

    if (strcmp(argv[i++], "-i")) {
        Usage();
    }
    ifindex = atoi(argv[i++]);

    while (i < argc) {
        if (!strcmp(argv[i], "-t")) {
            threadCount++;
        } else if (!strcmp(argv[i], "-p")) {
            if (++i >= argc) {
                Usage();
            }
            udpDestPort = (UINT16)atoi(argv[i]);
        } else if (!strcmp(argv[i], "-d")) {
            if (++i >= argc) {
                Usage();
            }
            duration = atoi(argv[i]);
        } else if (!strcmp(argv[i], "-v")) {
            verbose = TRUE;
        } else if (!_stricmp(argv[i], "-lp")) {
            largePages = TRUE;
            EnableLargePages();
        } else if (threadCount == 0) {
            Usage();
        }

        ++i;
    }

    if (ifindex == -1) {
        Usage();
    }

    if (threadCount == 0) {
        Usage();
    }

    threads = calloc(threadCount, sizeof(*threads));
    ASSERT_FRE(threads != NULL);

    INT tStart = -1;
    INT tIndex = 0;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) {
            if (tStart != -1) {
                ParseThreadArgs(&threads[tIndex++], i - tStart, &argv[tStart]);
            }
            tStart = i + 1;
        }
    }
    ParseThreadArgs(&threads[tIndex++], argc - tStart, &argv[tStart]);

    *ThreadsPtr = threads;
    *ThreadCountPtr = threadCount;
}

HRESULT
SetThreadAffinities(
    MY_THREAD *Thread
    )
{
    if (Thread->nodeAffinity != DEFAULT_NODE_AFFINITY) {
        GROUP_AFFINITY group;

        printf_verbose("setting node affinity %d\n", Thread->nodeAffinity);
        if (!GetNumaNodeProcessorMaskEx((USHORT)Thread->nodeAffinity, &group)) {
            assert(FALSE);
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
            assert(FALSE);
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (Thread->group != DEFAULT_GROUP) {
        GROUP_AFFINITY group = {0};

        printf_verbose("setting CPU affinity mask 0x%llu\n", Thread->cpuAffinity);
        printf_verbose("setting group affinity %d\n", Thread->group);
        group.Mask = Thread->cpuAffinity;
        group.Group = (WORD)Thread->group;
        if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
            assert(FALSE);
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (Thread->idealCpu != DEFAULT_IDEAL_CPU) {
        DWORD oldCpu;
        printf_verbose("setting ideal CPU %d\n", Thread->idealCpu);
        oldCpu = SetThreadIdealProcessor(GetCurrentThread(), Thread->idealCpu);
        assert(oldCpu != -1);
        if (oldCpu == -1) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    return S_OK;
}

DWORD
WINAPI
DoThread(
    LPVOID lpThreadParameter
    )
{
    MY_THREAD *thread = lpThreadParameter;
    HRESULT res;

    // Affinitize ASAP: memory allocations implicitly target the current
    // NUMA node, including kernel XDP allocations.
    res = SetThreadAffinities(thread);
    ASSERT_FRE(res == S_OK);

    if (mode == ModeRx) {
        DoRxMode(thread);
    } else if (mode == ModeTx) {
        DoTxMode(thread);
    } else if (mode == ModeFwd) {
        DoFwdMode(thread);
    } else if (mode == ModeLat) {
        DoLatMode(thread);
    }

    return 0;
}

BOOL
WINAPI
ConsoleCtrlHandler(
    DWORD CtrlType
    )
{
    UNREFERENCED_PARAMETER(CtrlType);

    // Force graceful exit.
    duration = 0;
    SetEvent(periodicStatsEvent);

    return TRUE;
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    MY_THREAD *threads;
    UINT32 threadCount;

    ParseArgs(&threads, &threadCount, argc, argv);

    ASSERT_FRE(SUCCEEDED(XdpOpenApi(XDP_API_VERSION_1, &XdpApi)));

    periodicStatsEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    ASSERT_FRE(periodicStatsEvent != NULL);

    ASSERT_FRE(SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE));

    for (UINT32 tIndex = 0; tIndex < threadCount; tIndex++) {
        threads[tIndex].readyEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        ASSERT_FRE(threads[tIndex].readyEvent != NULL);
        threads[tIndex].threadHandle =
            CreateThread(NULL, 0, DoThread, &threads[tIndex], 0, NULL);
        ASSERT_FRE(threads[tIndex].threadHandle != NULL);
        WaitForSingleObject(threads[tIndex].readyEvent, INFINITE);
    }

    while (duration-- > 0) {
        WaitForSingleObject(periodicStatsEvent, 1000);
        for (UINT32 tIndex = 0; tIndex < threadCount; tIndex++) {
            MY_THREAD *Thread = &threads[tIndex];
            for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
                ProcessPeriodicStats(&Thread->queues[qIndex]);
            }
        }
    }

    WriteBooleanNoFence(&done, TRUE);

    for (UINT32 tIndex = 0; tIndex < threadCount; tIndex++) {
        MY_THREAD *Thread = &threads[tIndex];
        WaitForSingleObject(Thread->threadHandle, INFINITE);
        for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
            PrintFinalStats(&Thread->queues[qIndex]);
        }
    }

    XdpCloseApi(XdpApi);

    return 0;
}
