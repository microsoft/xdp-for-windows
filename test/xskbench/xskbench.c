//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#if defined(_KERNEL_MODE)
#include <ntddk.h>
#include <intsafe.h>
#include <stdlib.h>
#include "trace.h"
#include "xskbench_kernel.h"
#else
#include <windows.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "xskbench_user.h"
#endif
#include <afxdp_helper.h>
#include <afxdp_experimental.h>
#include <xdpapi.h>
#include "cxplat.h"
#include "platform.h"
#include "xskbench_common.h"

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#define SHALLOW_STR_OF(x) #x
#define STR_OF(x) SHALLOW_STR_OF(x)

#define STRUCT_FIELD_OFFSET(structPtr, field) \
    ((UCHAR *)&(structPtr)->field - (UCHAR *)(structPtr))

#if defined (_KERNEL_MODE)
extern XDP_API_PROVIDER_DISPATCH *XdpApi;
#else
extern XDP_API_TABLE *XdpApi;
#endif // defined (_KERNEL_MODE)

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

#define Usage() { PrintUsage(__LINE__); goto Fail; }

#define WAIT_DRIVER_TIMEOUT_MS 1050

typedef enum {
    ModeRx,
    ModeTx,
    ModeFwd,
    ModeLat,
} MODE;

typedef struct {
    CXPLAT_THREAD threadHandle;
    CXPLAT_EVENT readyEvent;
    LONG nodeAffinity;
    LONG group;
    LONG idealCpu;
    UINT32 yieldCount;
    DWORD_PTR cpuAffinity;
    BOOLEAN wait;

    UINT32 queueCount;
    MY_QUEUE *queues;
} MY_THREAD;

INT ifindex = -1;
UINT16 udpDestPort = DEFAULT_UDP_DEST_PORT;
ULONG duration = DEFAULT_DURATION;
BOOLEAN verbose = FALSE;
BOOLEAN done = FALSE;
BOOLEAN threadSetupFailed;
BOOLEAN largePages = FALSE;
MODE mode;
CHAR *modestr;
CXPLAT_EVENT periodicStatsEvent;
int _fltused = 0;

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

BOOLEAN
AttachXdpProgram(
    MY_QUEUE *Queue
    )
{
    XDP_RULE rule = {0};
    UINT32 flags = 0;
    XDP_HOOK_ID hookId;
    UINT32 hookSize = sizeof(hookId);
    XDP_STATUS res;

    if (!Queue->flags.rx) {
        return TRUE;
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
    ASSERT_FRE(XDP_SUCCEEDED(res));
    ASSERT_FRE(hookSize == sizeof(hookId));

    res =
        CxPlatXdpCreateProgram(
            ifindex, &hookId, Queue->queueId,
            flags, &rule, 1, &Queue->rxProgram);
    if (XDP_FAILED(res)) {
        printf_error("XdpCreateProgram failed: %d\n", res);
        return FALSE;
    }

    return TRUE;
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
    _In_opt_z_ const CHAR *Hex
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

BOOLEAN
SetupSock(
    INT IfIndex,
    MY_QUEUE *Queue
    )
{
    XDP_STATUS res;
    UINT32 bindFlags = 0;

    printf_verbose("creating sock\n");
    res = CxPlatXskCreate(Queue->sock);
    if (XDP_FAILED(res)) {
        printf_error("XskCreate failed: %d\n", res);
        return FALSE;
    }

    printf_verbose("XDP_UMEM_REG\n");

    Queue->umemReg.ChunkSize = Queue->umemchunksize;
    Queue->umemReg.Headroom = Queue->umemheadroom;
    Queue->umemReg.TotalSize = Queue->umemsize;

    if (largePages) {
        CxPlatAlignMemory(&Queue->umemReg);
    }

    Queue->umemReg.Address =
        CXPLAT_VIRTUAL_ALLOC(
            Queue->umemReg.TotalSize,
            (largePages ? MEM_LARGE_PAGES : 0) | MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE,
            POOLTAG_UMEM);
    if (Queue->umemReg.Address == NULL) {
        printf_error("UMEM allocation of %llu bytes failed\n", Queue->umemReg.TotalSize);
        return FALSE;
    }

    res =
        XdpApi->XskSetSockopt(
            Queue->sock, XSK_SOCKOPT_UMEM_REG, &Queue->umemReg,
            sizeof(Queue->umemReg));
    ASSERT_FRE(XDP_SUCCEEDED(res));

    printf_verbose("configuring fill ring with size %d\n", Queue->ringsize);
    res =
        XdpApi->XskSetSockopt(
            Queue->sock, XSK_SOCKOPT_RX_FILL_RING_SIZE, &Queue->ringsize,
            sizeof(Queue->ringsize));
    ASSERT_FRE(XDP_SUCCEEDED(res));

    printf_verbose("configuring completion ring with size %d\n", Queue->ringsize);
    res =
        XdpApi->XskSetSockopt(
            Queue->sock, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &Queue->ringsize,
            sizeof(Queue->ringsize));
    ASSERT_FRE(XDP_SUCCEEDED(res));

    if (Queue->flags.rx) {
        printf_verbose("configuring rx ring with size %d\n", Queue->ringsize);
        res =
            XdpApi->XskSetSockopt(
                Queue->sock, XSK_SOCKOPT_RX_RING_SIZE, &Queue->ringsize,
                sizeof(Queue->ringsize));
        ASSERT_FRE(XDP_SUCCEEDED(res));
        bindFlags |= XSK_BIND_FLAG_RX;
    }
    if (Queue->flags.tx) {
        printf_verbose("configuring tx ring with size %d\n", Queue->ringsize);
        res =
            XdpApi->XskSetSockopt(
                Queue->sock, XSK_SOCKOPT_TX_RING_SIZE, &Queue->ringsize,
                sizeof(Queue->ringsize));
        ASSERT_FRE(XDP_SUCCEEDED(res));
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
        ASSERT_FRE(XDP_SUCCEEDED(res));
    }

    if (Queue->flags.txInspect) {
        XDP_HOOK_ID hookId = {0};
        hookId.Layer = XDP_HOOK_L2;
        hookId.Direction = XDP_HOOK_TX;
        hookId.SubLayer = XDP_HOOK_INSPECT;

        printf_verbose("configuring rx from tx inspect\n");
        res = XdpApi->XskSetSockopt(Queue->sock, XSK_SOCKOPT_RX_HOOK_ID, &hookId, sizeof(hookId));
        ASSERT_FRE(XDP_SUCCEEDED(res));
    }

    printf_verbose(
        "binding sock to ifindex %d queueId %d flags 0x%x\n", IfIndex, Queue->queueId, bindFlags);
    res = XdpApi->XskBind(Queue->sock, IfIndex, Queue->queueId, bindFlags);
    if (XDP_FAILED(res)) {
        printf_error("XskBind failed: %d\n", res);
        return FALSE;
    }

    printf_verbose("activating sock\n");
    res = XdpApi->XskActivate(Queue->sock, 0);
    ASSERT_FRE(XDP_SUCCEEDED(res));

    printf_verbose("XSK_SOCKOPT_RING_INFO\n");
    XSK_RING_INFO_SET infoSet = { 0 };
    UINT32 ringInfoSize = sizeof(infoSet);
    res = XdpApi->XskGetSockopt(Queue->sock, XSK_SOCKOPT_RING_INFO, &infoSet, &ringInfoSize);
    ASSERT_FRE(XDP_SUCCEEDED(res));
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
    ASSERT_FRE(XDP_SUCCEEDED(res));

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
        CXPLAT_ALLOC_NONPAGED(
            sizeof(*FreeRingLayout) + numDescriptors * sizeof(*FreeRingLayout->Descriptors),
            POOLTAG_FREERING);
    ASSERT_FRE(FreeRingLayout != NULL);
    RtlZeroMemory(
        FreeRingLayout,
        sizeof(*FreeRingLayout) + numDescriptors * sizeof(*FreeRingLayout->Descriptors));

    XSK_RING_INFO freeRingInfo = {0};
    freeRingInfo.Ring = (BYTE *)FreeRingLayout;
    freeRingInfo.ProducerIndexOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Producer);
    freeRingInfo.ConsumerIndexOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Consumer);
    freeRingInfo.FlagsOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Flags);
    freeRingInfo.DescriptorsOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Descriptors[0]);
    freeRingInfo.Size = numDescriptors;
    freeRingInfo.ElementStride = sizeof(*FreeRingLayout->Descriptors);
    XskRingInitialize(&Queue->freeRing, &freeRingInfo);
    Queue->freeRingLayout = FreeRingLayout;
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

    if (!AttachXdpProgram(Queue)) {
        return FALSE;
    }

    return TRUE;
}

_Kernel_float_used_
VOID
ProcessPeriodicStats(
    MY_QUEUE *Queue
    )
{
    UINT64 currentTick = CxPlatTimeEpochMs64();
    UINT64 tickDiff = currentTick - Queue->lastTick;
    UINT64 packetCount;
    UINT64 packetDiff;
    UINT64 kpps;

    if (tickDiff == 0) {
        return;
    }

    packetCount = Queue->packetCount;
    packetDiff = packetCount - Queue->lastPacketCount;
    kpps = (packetDiff) ? packetDiff / tickDiff : 0;

    if (Queue->flags.periodicStats) {
        XSK_STATISTICS stats;
        UINT32 optSize = sizeof(stats);
        ULONGLONG pokesRequested = Queue->pokesRequestedCount;
        ULONGLONG pokesPerformed = Queue->pokesPerformedCount;
        ULONGLONG pokesRequestedDiff;
        ULONGLONG pokesPerformedDiff;
        ULONGLONG pokesAvoidedPercentage;
        ULONGLONG rxDropDiff;
        ULONGLONG rxDropKpps;

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

        XDP_STATUS res =
            XdpApi->XskGetSockopt(Queue->sock, XSK_SOCKOPT_STATISTICS, &stats, &optSize);
        ASSERT_FRE(XDP_SUCCEEDED(res));
        ASSERT_FRE(optSize == sizeof(stats));

        rxDropDiff = stats.RxDropped - Queue->lastRxDropCount;
        rxDropKpps = rxDropDiff ? rxDropDiff / tickDiff : 0;
        Queue->lastRxDropCount = stats.RxDropped;

        printf("%s[%d]: %9llu kpps %9llu rxDropKpps rxDrop:%llu rxTrunc:%llu "
            "rxBadDesc:%llu txBadDesc:%llu pokesAvoided:%llu%%\n",
            modestr, Queue->queueId, kpps, rxDropKpps, stats.RxDropped, stats.RxTruncated,
            stats.RxInvalidDescriptors, stats.TxInvalidDescriptors,
            pokesAvoidedPercentage);

        Queue->lastPokesRequestedCount = pokesRequested;
        Queue->lastPokesPerformedCount = pokesPerformed;
    }

    Queue->statsArray[Queue->currStatsArrayIdx++ % STATS_ARRAY_SIZE] = (double)kpps;
    Queue->lastPacketCount = packetCount;
    Queue->lastTick = currentTick;
}

INT
LatCmp(
    const VOID *A,
    const VOID *B
    )
{
    const UINT64 *a = A;
    const UINT64 *b = B;
    return (*a > *b) - (*a < *b);
}

_Kernel_float_used_
VOID
PrintFinalLatStats(
    MY_QUEUE *Queue
    )
{
    qsort(Queue->latSamples, Queue->latIndex, sizeof(*Queue->latSamples), LatCmp);

    for (UINT32 i = 0; i < Queue->latIndex; i++) {
        Queue->latSamples[i] = CxPlatTimePlatToUs64(Queue->latSamples[i]);
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

_Kernel_float_used_
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

        stdDev += (Queue->statsArray[i] - avg) * (Queue->statsArray[i] - avg);
    }

    stdDev = sqrt(stdDev / (numEntries - 1));

    printf("%-3s[%d]: avg=%08llu stddev=%08llu min=%08llu max=%08llu Kpps\n",
        modestr, Queue->queueId, (UINT64)avg, (UINT64)stdDev, (UINT64)min, (UINT64)max);

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
    XDP_STATUS res;
    XSK_NOTIFY_RESULT_FLAGS notifyResult;

    if (Queue->flags.optimizePoking) {
        //
        // Ensure poke flags are read after writing producer/consumer indices.
        //
        XdpBarrierBetweenReleaseAndAcquire();

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
            ASSERT_FRE(res == XDP_STATUS_SUCCESS || res == XDP_STATUS_TIMEOUT);
        } else {
            ASSERT_FRE(res == XDP_STATUS_SUCCESS);
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
        if (!SetupSock(ifindex, queue)) {
            threadSetupFailed = TRUE;
            continue;
        }
        queue->lastTick = CxPlatTimeEpochMs64();
    }

    printf("Receiving...\n");
    CxPlatEventSet(Thread->readyEvent);

    if (!threadSetupFailed) {
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
        ASSERT(Queue->umemReg.Headroom <= MAXUINT16);
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
        if (!SetupSock(ifindex, queue)) {
            threadSetupFailed = TRUE;
            continue;
        }
        queue->lastTick = CxPlatTimeEpochMs64();
    }

    printf("Sending...\n");
    CxPlatEventSet(Thread->readyEvent);

    if (!threadSetupFailed) {
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
        if (!SetupSock(ifindex, queue)) {
            threadSetupFailed = TRUE;
            continue;
        }
        queue->lastTick = CxPlatTimeEpochMs64();
    }

    printf("Forwarding...\n");
    CxPlatEventSet(Thread->readyEvent);

    if (!threadSetupFailed) {
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
        UINT64 NowQpc = CxPlatTimePlat();

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

            printf_verbose("latency: %lld\n", NowQpc - *Timestamp);

            if (Queue->latIndex < Queue->latSamplesCount) {
                Queue->latSamples[Queue->latIndex++] = NowQpc - *Timestamp;
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

        if (XskRingProducerReserve(&Queue->txRing, MAXUINT32, &producerIndex) !=
                Queue->txRing.Size) {
            notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
        }
    }

    //
    // Move frames from the free ring to the TX ring, stamping the current time
    // onto each frame.
    //
    available =
        RingPairReserve(
            &Queue->freeRing, &consumerIndex, &Queue->txRing, &producerIndex, Queue->iobatchsize);
    if (available > 0) {
        UINT64 NowQpc = CxPlatTimePlat();

        for (UINT32 i = 0; i < available; i++) {
            UINT64 *freeDesc = XskRingGetElement(&Queue->freeRing, consumerIndex++);
            XSK_BUFFER_DESCRIPTOR *txDesc = XskRingGetElement(&Queue->txRing, producerIndex++);

            INT64 UNALIGNED *Timestamp = (INT64 UNALIGNED *)
                ((CHAR*)Queue->umemReg.Address + *freeDesc +
                    Queue->umemReg.Headroom + Queue->txPatternLength);
            *Timestamp = NowQpc;

            txDesc->Address.BaseAddress = *freeDesc;
            ASSERT(Queue->umemReg.Headroom <= MAXUINT16);
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
        if (!SetupSock(ifindex, queue)) {
            threadSetupFailed = TRUE;
            continue;
        }
        queue->lastTick = CxPlatTimeEpochMs64();

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
    CxPlatEventSet(Thread->readyEvent);

    if (!threadSetupFailed) {
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
}

VOID
PrintUsage(
    INT Line
    )
{
    printf_error("Line:%d\n", Line);
    printf_error(HELP);
}

BOOLEAN
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
            Queue->txPattern = CXPLAT_ALLOC_NONPAGED(Queue->txPatternLength, POOLTAG_TXPATTERN);
            ASSERT_FRE(Queue->txPattern != NULL);
            RtlZeroMemory(Queue->txPattern, Queue->txPatternLength);
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

        Queue->latSamples =
            CXPLAT_ALLOC_NONPAGED(
                Queue->latSamplesCount * sizeof(*Queue->latSamples), POOLTAG_LATSAMPLES);
        ASSERT_FRE(Queue->latSamples != NULL);
        RtlZeroMemory(Queue->latSamples, Queue->latSamplesCount * sizeof(*Queue->latSamples));
    }

    return TRUE;
Fail:
    return FALSE;
}

BOOLEAN
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

    Thread->queues =
        CXPLAT_ALLOC_NONPAGED(Thread->queueCount * sizeof(*Thread->queues), POOLTAG_QUEUES);
    ASSERT_FRE(Thread->queues != NULL);
    RtlZeroMemory(Thread->queues, Thread->queueCount * sizeof(*Thread->queues));

    INT qStart = -1;
    INT qIndex = 0;
    for (INT i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) {
            if (qStart != -1) {
                if (!ParseQueueArgs(&Thread->queues[qIndex++], i - qStart, &argv[qStart])) {
                    goto Fail;
                }
            }
            qStart = i + 1;
        }
    }
    if (!ParseQueueArgs(&Thread->queues[qIndex++], argc - qStart, &argv[qStart])) {
        goto Fail;
    }

    return TRUE;
Fail:
    return FALSE;
}

BOOLEAN
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
            if (!CxPlatEnableLargePages()) {
                goto Fail;
            }
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

    threads = CXPLAT_ALLOC_NONPAGED(threadCount * sizeof(*threads), POOLTAG_THREADS);
    ASSERT_FRE(threads != NULL);
    RtlZeroMemory(threads, threadCount * sizeof(*threads));

    INT tStart = -1;
    INT tIndex = 0;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) {
            if (tStart != -1) {
                if (!ParseThreadArgs(&threads[tIndex++], i - tStart, &argv[tStart])) {
                    goto Fail;
                }
            }
            tStart = i + 1;
        }
    }
    if (!ParseThreadArgs(&threads[tIndex++], argc - tStart, &argv[tStart])) {
        goto Fail;
    }

    *ThreadsPtr = threads;
    *ThreadCountPtr = threadCount;

    return TRUE;
Fail:
    return FALSE;
}

BOOLEAN
SetThreadAffinities(
    MY_THREAD *Thread
    )
{
    if (Thread->nodeAffinity != DEFAULT_NODE_AFFINITY) {
        printf_verbose("setting node affinity %d\n", Thread->nodeAffinity);
        if (!CxPlatSetThreadNodeAffinity(Thread->nodeAffinity)) {
            return FALSE;
        }
    }

    if (Thread->group != DEFAULT_GROUP) {

        printf_verbose("setting CPU affinity mask 0x%llu\n", Thread->cpuAffinity);
        printf_verbose("setting group affinity %d\n", Thread->group);
        if (!CxPlatSetThreadGroupAffinity(Thread->group, Thread->cpuAffinity)) {
            return FALSE;
        }
    }

    return TRUE;
}

CXPLAT_THREAD_CALLBACK(DoThread, Context)
{
    MY_THREAD *thread = Context;
    BOOLEAN res;

    // Affinitize ASAP: memory allocations implicitly target the current
    // NUMA node, including kernel XDP allocations.
    res = SetThreadAffinities(thread);
    ASSERT_FRE(res);

    if (mode == ModeRx) {
        DoRxMode(thread);
    } else if (mode == ModeTx) {
        DoTxMode(thread);
    } else if (mode == ModeFwd) {
        DoFwdMode(thread);
    } else if (mode == ModeLat) {
        DoLatMode(thread);
    }

    CXPLAT_THREAD_RETURN(0);
}

VOID
XskBenchInitialize(
    VOID
    )
{
    CxPlatEventInitialize(&periodicStatsEvent, TRUE, FALSE);
}

VOID
XskBenchCtrlHandler(
    VOID
    )
{
    // Force graceful exit.
    duration = 0;
    CxPlatEventSet(periodicStatsEvent);
}

INT
__cdecl
XskBenchStart(
    INT argc,
    CHAR **argv
    )
{
    MY_THREAD *threads = NULL;
    UINT32 threadCount;

    if (!ParseArgs(&threads, &threadCount, argc, argv)) {
        goto Exit;
    }

    CxPlatXdpApiInitialize();

    threadSetupFailed = FALSE;

    for (UINT32 tIndex = 0; tIndex < threadCount; tIndex++) {
        CxPlatEventInitialize(&threads[tIndex].readyEvent, TRUE, FALSE);

        CXPLAT_THREAD_CONFIG ThreadConfig = {
            0,
            0,
            "",
            DoThread,
            &threads[tIndex]
        };

        if (threads[tIndex].idealCpu != DEFAULT_IDEAL_CPU) {
            ThreadConfig.IdealProcessor = (UINT16)threads[tIndex].idealCpu;
            ThreadConfig.Flags |= CXPLAT_THREAD_FLAG_SET_IDEAL_PROC;
        }

        XDP_STATUS Status = CxPlatThreadCreate(&ThreadConfig, &threads[tIndex].threadHandle);
        ASSERT_FRE(XDP_SUCCEEDED(Status));

        CxPlatEventWaitForever(threads[tIndex].readyEvent);
    }

    if (!threadSetupFailed) {
        while (duration-- > 0) {
            CxPlatEventWaitWithTimeout(periodicStatsEvent, 1000);
            for (UINT32 tIndex = 0; tIndex < threadCount; tIndex++) {
                MY_THREAD *Thread = &threads[tIndex];
                for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
                    CxPlatPrintStats(&Thread->queues[qIndex]);
                }
            }
        }
    }

    WriteBooleanNoFence(&done, TRUE);

    for (UINT32 tIndex = 0; tIndex < threadCount; tIndex++) {
        MY_THREAD *Thread = &threads[tIndex];
        CxPlatThreadWaitForever(&Thread->threadHandle);
        CxPlatThreadDelete(&Thread->threadHandle);
        for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
            MY_QUEUE *Queue = &Thread->queues[qIndex];
            CxPlatPrintStats(Queue);
            CxPlatQueueCleanup(Queue);
        }
    }

    CxPlatXdpApiUninitialize();

Exit:
#pragma warning(disable:6001) // using uninitialized memory
    if (threads != NULL) {
        for (UINT32 tIndex = 0; tIndex < threadCount; tIndex++) {
            MY_THREAD *Thread = &threads[tIndex];
            if (Thread == NULL) {
                continue;
            }
            for (UINT32 qIndex = 0; qIndex < Thread->queueCount; qIndex++) {
                MY_QUEUE *Queue = &Thread->queues[qIndex];
                if (Queue == NULL) {
                    continue;
                }
                if (Queue->txPattern != NULL) {
                    CXPLAT_FREE(Queue->txPattern, POOLTAG_TXPATTERN);
                }
                if (Queue->latSamples != NULL) {
                    CXPLAT_FREE(Queue->latSamples, POOLTAG_LATSAMPLES);
                }
            }
            CXPLAT_FREE(Thread->queues, POOLTAG_QUEUES);
        }
        CXPLAT_FREE(threads, POOLTAG_THREADS);
    }

    WriteBooleanNoFence(&done, FALSE);

    return 0;
}
