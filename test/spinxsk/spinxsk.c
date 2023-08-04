//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union

#include <windows.h>
#include <iphlpapi.h>
#include <assert.h>
#include <crtdbg.h>
#include <stdio.h>
#define _CRT_RAND_S
#include <stdlib.h>
#include <afxdp_helper.h>
#include <afxdp_experimental.h>
#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include <xdpapi_internal.h>
#include <xdprtl.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "trace.h"
#include "util.h"
#include "spinxsk.tmh"

#define SHALLOW_STR_OF(x) #x
#define STR_OF(x) SHALLOW_STR_OF(x)

#define STRUCT_FIELD_OFFSET(structPtr, field) \
    ((UCHAR *)&(structPtr)->field - (UCHAR *)(structPtr))

#define DEFAULT_IO_BATCH 1
#define DEFAULT_DURATION ULONG_MAX
#define DEFAULT_QUEUE_COUNT 4
#define DEFAULT_FUZZER_COUNT 3
#define DEFAULT_SUCCESS_THRESHOLD 50

CHAR *HELP =
"spinxsk.exe -IfIndex <ifindex> [OPTIONS]\n"
"\n"
"OPTIONS: \n"
"   -Minutes <minutes>    Duration of execution in minutes\n"
"                         Default: infinite\n"
"   -Stats                Periodic socket statistics output\n"
"                         Default: off\n"
"   -Verbose              Verbose logging\n"
"                         Default: off\n"
"   -QueueCount <count>   Number of queues to spin\n"
"                         Default: " STR_OF(DEFAULT_QUEUE_COUNT) "\n"
"   -FuzzerCount <count>  Number of fuzzer threads per queue\n"
"                         Default: " STR_OF(DEFAULT_FUZZER_COUNT) "\n"
"   -CleanDatapath        Avoid actions that invalidate the datapath\n"
"                         Default: off\n"
"   -WatchdogCmd <cmd>    Execute a system command after a watchdog violation\n"
"                         If cmd is \"break\" then a debug breakpoint is\n"
"                         triggered instead\n"
"                         Default: \"\"\n"
"   -SuccessThresholdPercent <count> Minimum socket success rate, percent\n"
"                         Default: " STR_OF(DEFAULT_SUCCESS_THRESHOLD) "\n"
"   -EnableEbpf           Enables eBPF testing\n"
"                         Default: off\n"
;

#define ASSERT_FRE(expr) \
    if (!(expr)) { printf("("#expr") failed line %d\n", __LINE__);  exit(1);}

#define Usage() PrintUsage(__LINE__)

#ifndef TraceVerbose
#define TraceVerbose(s, ...) \
    if (verbose) { \
        SYSTEMTIME system_time; \
        char timestamp_buf[21] = { 0 }; \
        GetLocalTime(&system_time); \
        sprintf_s(timestamp_buf, ARRAYSIZE(timestamp_buf), "%d/%d/%d %d:%d:%d ", \
            system_time.wMonth, system_time.wDay, system_time.wYear, \
            system_time.wHour, system_time.wMinute, system_time.wSecond); \
        printf("%s" s, timestamp_buf, __VA_ARGS__); \
    }
#endif

#define WAIT_DRIVER_TIMEOUT_MS 1050
#define ADMIN_THREAD_TIMEOUT_SEC 1
#define WATCHDOG_THREAD_TIMEOUT_SEC 10

typedef struct QUEUE_CONTEXT QUEUE_CONTEXT;

typedef enum {
    XdpModeSystem,
    XdpModeGeneric,
    XdpModeNative,
} XDP_MODE;

CHAR *XdpModeToString[] = {
    "System",
    "Generic",
    "Native",
};

typedef struct {
    //
    // Describes the configured end state.
    //
    BOOLEAN sockRx;
    BOOLEAN sockTx;
    BOOLEAN sharedUmemSockRx;
    BOOLEAN sharedUmemSockTx;
    BOOLEAN sockRxTxSeparateThreads;

    //
    // Describes progress towards the end state.
    //
    BOOLEAN isSockRxSet;
    BOOLEAN isSockTxSet;
    BOOLEAN isSharedUmemSockRxSet;
    BOOLEAN isSharedUmemSockTxSet;
    BOOLEAN isSockBound;
    BOOLEAN isSockActivated;
    BOOLEAN isSharedUmemSockBound;
    BOOLEAN isSharedUmemSockActivated;
    BOOLEAN isUmemRegistered;
    HANDLE completeEvent;
} SCENARIO_CONFIG;

typedef enum {
    ThreadStateRun,
    ThreadStatePause,
    ThreadStateReturn,
} THREAD_STATE;

typedef struct {
    THREAD_STATE state;
    QUEUE_CONTEXT *queue;
} XSK_DATAPATH_SHARED;

typedef enum {
    ProgramHandleXdp,
    ProgramHandleEbpf,
} PROGRAM_HANDLE_TYPE;

typedef struct {
    PROGRAM_HANDLE_TYPE Type;
    union {
        HANDLE Handle;
        struct bpf_object *BpfObject;
    };
} PROGRAM_HANDLE;

typedef struct {
    CRITICAL_SECTION Lock;
    UINT32 HandleCount;
    PROGRAM_HANDLE Handles[8];
} XSK_PROGRAM_SET;

typedef struct {
    HANDLE threadHandle;
    XSK_DATAPATH_SHARED *shared;

    HANDLE sock;
    XSK_PROGRAM_SET *rxProgramSet;

    struct {
        BOOLEAN rx : 1;
        BOOLEAN tx : 1;
        BOOLEAN wait : 1;
    } flags;
    ULONG txiosize;
    ULONG iobatchsize;
    XSK_POLL_MODE pollMode;

    ULONGLONG rxPacketCount;
    ULONGLONG txPacketCount;

    ULONGLONG rxWatchdogPerfCount;
    ULONGLONG txWatchdogPerfCount;

    XSK_RING rxRing;
    XSK_RING txRing;
    XSK_RING fillRing;
    XSK_RING compRing;
    XSK_RING rxFreeRing;
    XSK_RING txFreeRing;

    BYTE *rxFreeRingBase;
    BYTE *txFreeRingBase;
} XSK_DATAPATH_WORKER;

typedef struct {
    THREAD_STATE state;
    HANDLE pauseEvent;
    QUEUE_CONTEXT *queue;
} XSK_FUZZER_SHARED;

typedef struct {
    HANDLE threadHandle;
    XSK_FUZZER_SHARED *shared;
    HANDLE fuzzerInterface;
} XSK_FUZZER_WORKER;

struct QUEUE_CONTEXT {
    UINT32 queueId;

    CONST XDP_API_TABLE *xdpApi;
    XDP_RSS_GET_CAPABILITIES_FN *XdpRssGetCapabilities;
    XDP_RSS_SET_FN *XdpRssSet;
    XDP_RSS_GET_FN *XdpRssGet;
    XDP_QEO_SET_FN *XdpQeoSet;
    HANDLE sock;
    HANDLE sharedUmemSock;
    HANDLE rss;
    SRWLOCK rssLock;

    XSK_PROGRAM_SET rxProgramSet;
    XSK_PROGRAM_SET sharedUmemRxProgramSet;

    XSK_UMEM_REG umemReg;
    XDP_MODE xdpMode;
    SCENARIO_CONFIG scenarioConfig;

    XSK_FUZZER_SHARED fuzzerShared;
    UINT32 fuzzerCount;
    XSK_FUZZER_WORKER *fuzzers;

    XSK_DATAPATH_SHARED datapathShared;
    XSK_DATAPATH_WORKER datapath1;
    XSK_DATAPATH_WORKER datapath2;
};

typedef struct {
    ULONG initSuccess;
    ULONG umemSuccess;
    ULONG umemTotal;
    ULONG rxSuccess;
    ULONG rxTotal;
    ULONG txSuccess;
    ULONG txTotal;
    ULONG bindSuccess;
    ULONG bindTotal;
    ULONG activateSuccess;
    ULONG activateTotal;
    ULONG sharedRxSuccess;
    ULONG sharedRxTotal;
    ULONG sharedTxSuccess;
    ULONG sharedTxTotal;
    ULONG sharedBindSuccess;
    ULONG sharedBindTotal;
    ULONG sharedActivateSuccess;
    ULONG sharedActivateTotal;
    ULONG setupSuccess;
} SETUP_STATS;

typedef struct {
    HANDLE threadHandle;
    UINT32 queueId;
    ULONGLONG watchdogPerfCount;
    SETUP_STATS setupStats;
} QUEUE_WORKER;

INT ifindex = -1;
ULONG duration = DEFAULT_DURATION;
BOOLEAN verbose = FALSE;
BOOLEAN cleanDatapath = FALSE;
BOOLEAN done = FALSE;
BOOLEAN extraStats = FALSE;
BOOLEAN enableEbpf = FALSE;
UINT8 successThresholdPercent = DEFAULT_SUCCESS_THRESHOLD;
HANDLE stopEvent;
HANDLE workersDoneEvent;
QUEUE_WORKER *queueWorkers;
UINT32 queueCount = DEFAULT_QUEUE_COUNT;
UINT32 fuzzerCount = DEFAULT_FUZZER_COUNT;
ULONGLONG perfFreq;
CONST CHAR *watchdogCmd = "";
CONST CHAR *powershellPrefix;

ULONG
RandUlong(
    VOID
    )
{
    unsigned int r = 0;
    rand_s(&r);
    return r;
}

ULONG
Pct(
    ULONG Dividend,
    ULONG Divisor
    )
{
    return (Divisor == 0) ? 0 : Dividend * 100 / Divisor;
}

BOOLEAN
ScenarioConfigActivateReady(
    _In_ CONST SCENARIO_CONFIG *ScenarioConfig
    )
{
    if (ScenarioConfig->sockRx &&
        !ReadBooleanAcquire(&ScenarioConfig->isSockRxSet)) {
        return FALSE;
    }
    if (ScenarioConfig->sockTx &&
        !ReadBooleanAcquire(&ScenarioConfig->isSockTxSet)) {
        return FALSE;
    }
    if (ScenarioConfig->sharedUmemSockRx &&
        !ReadBooleanAcquire(&ScenarioConfig->isSharedUmemSockRxSet)) {
        return FALSE;
    }
    if (ScenarioConfig->sharedUmemSockTx &&
        !ReadBooleanAcquire(&ScenarioConfig->isSharedUmemSockTxSet)) {
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
ScenarioConfigComplete(
    _In_ CONST SCENARIO_CONFIG *ScenarioConfig
    )
{
    if (!ReadBooleanAcquire(&ScenarioConfig->isUmemRegistered)) {
        return FALSE;
    }
    if (!ReadBooleanAcquire(&ScenarioConfig->isSockActivated)) {
        return FALSE;
    }
    if ((ScenarioConfig->sharedUmemSockRx || ScenarioConfig->sharedUmemSockTx) &&
        !ReadBooleanAcquire(&ScenarioConfig->isSharedUmemSockActivated)) {
        return FALSE;
    }

    return TRUE;
}

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

static
VOID
FuzzHookId(
    _Inout_ XDP_HOOK_ID *HookId
    )
{
    if (!(RandUlong() % 8)) {
        HookId->Layer = RandUlong() % 4;
    } else if (!(RandUlong() % 8)) {
        HookId->Layer = RandUlong();
    }

    if (!(RandUlong() % 8)) {
        HookId->Direction = RandUlong() % 4;
    } else if (!(RandUlong() % 8)) {
        HookId->Direction = RandUlong();
    }

    if (!(RandUlong() % 8)) {
        HookId->SubLayer = RandUlong() % 4;
    } else if (!(RandUlong() % 8)) {
        HookId->SubLayer = RandUlong();
    }
}

HRESULT
AttachXdpEbpfProgram(
    _In_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _Inout_ XSK_PROGRAM_SET *RxProgramSet
    )
{
    HRESULT Result;
    CHAR Path[MAX_PATH];
    const CHAR *ProgramRelativePath = NULL;
    struct bpf_object *BpfObject = NULL;
    struct bpf_program *BpfProgram = NULL;
    NET_IFINDEX IfIndex = ifindex;
    int ProgramFd;
    int AttachFlags = 0;
    int OriginalThreadPriority;

    //
    // Since eBPF does not support per-queue programs, attach to the entire
    // interface.
    //
    UNREFERENCED_PARAMETER(Queue);

    //
    // Since eBPF does not yet support AF_XDP, ignore the socket.
    //
    UNREFERENCED_PARAMETER(Sock);

    if (!enableEbpf) {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    OriginalThreadPriority = GetThreadPriority(GetCurrentThread());
    ASSERT_FRE(OriginalThreadPriority != THREAD_PRIORITY_ERROR_RETURN);

    Result = GetCurrentBinaryPath(Path, sizeof(Path));
    if (FAILED(Result)) {
        goto Exit;
    }

    switch (RandUlong() % 3) {
    case 0:
        ProgramRelativePath = "\\bpf\\drop.o";
        break;
    case 1:
        ProgramRelativePath = "\\bpf\\pass.sys";
        break;
    case 2:
        ProgramRelativePath = "\\bpf\\l1fwd.o";
        break;
    default:
        ASSERT_FRE(FALSE);
    }

    ASSERT_FRE(strcat_s(Path, sizeof(Path), ProgramRelativePath) == 0);

    //
    // To work around control path delays caused by eBPF's epoch implementation,
    // boost this thread's priority when invoking eBPF APIs.
    //
    ASSERT_FRE(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST));

    TraceVerbose("bpf_object__open(%s)", Path);
    BpfObject = bpf_object__open(Path);
    if (BpfObject == NULL) {
        TraceVerbose("bpf_object__open(%s) failed: %d", Path, errno);
        Result = E_FAIL;
        goto Exit;
    }

    TraceVerbose("bpf_object__next_program(%p, %p)", BpfObject, NULL);
    BpfProgram = bpf_object__next_program(BpfObject, NULL);
    if (BpfProgram == NULL) {
        TraceVerbose("bpf_object__next_program failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    TraceVerbose("bpf_program__set_type(%p, %d)", BpfProgram, BPF_PROG_TYPE_XDP);
    if (bpf_program__set_type(BpfProgram, BPF_PROG_TYPE_XDP) < 0) {
        TraceVerbose("bpf_program__set_type failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    TraceVerbose("bpf_object__load(%p)", BpfObject);
    if (bpf_object__load(BpfObject) < 0) {
        TraceVerbose("bpf_object__load failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    TraceVerbose("bpf_program__fd(%p)", BpfProgram);
    ProgramFd = bpf_program__fd(BpfProgram);
    if (ProgramFd < 0) {
        TraceVerbose("bpf_program__fd failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    if ((RandUlong() % 2) == 0) {
        AttachFlags |= XDP_FLAGS_REPLACE;
    }

    if ((RandUlong() % 8) == 0) {
        //
        // Try IFI_UNSPECIFIED, which is an invalid interface index since XDP
        // does not support wildcards.
        //
        IfIndex = IFI_UNSPECIFIED;
    }

    TraceVerbose("bpf_xdp_attach(%u, %d, 0x%x, %p)", IfIndex, ProgramFd, AttachFlags, NULL);
    if (bpf_xdp_attach(IfIndex, ProgramFd, AttachFlags, NULL) < 0) {
        TraceVerbose("bpf_xdp_attach failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    EnterCriticalSection(&RxProgramSet->Lock);
    if (RxProgramSet->HandleCount < RTL_NUMBER_OF(RxProgramSet->Handles)) {
        RxProgramSet->Handles[RxProgramSet->HandleCount].Type = ProgramHandleEbpf;
        RxProgramSet->Handles[RxProgramSet->HandleCount].BpfObject = BpfObject;
        RxProgramSet->HandleCount++;
        Result = S_OK;
    } else {
        Result = E_NOT_SUFFICIENT_BUFFER;
    }
    LeaveCriticalSection(&RxProgramSet->Lock);

Exit:

    if (FAILED(Result)) {
        if (BpfObject != NULL) {
            TraceVerbose("bpf_object__close(%p)", BpfObject);
            bpf_object__close(BpfObject);
        }
    }

    ASSERT_FRE(SetThreadPriority(GetCurrentThread(), OriginalThreadPriority));

    return Result;
}

HRESULT
AttachXdpProgram(
    _In_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _In_ BOOLEAN RxRequired,
    _Inout_ XSK_PROGRAM_SET *RxProgramSet
    )
{
    XDP_RULE rule = {0};
    XDP_HOOK_ID hookId = {0};
    UINT32 hookIdSize = sizeof(hookId);
    UINT32 flags = 0;
    HANDLE handle;
    HRESULT res;
    UINT8 *PortSet = NULL;

    if (RxRequired) {
        rule.Match = XDP_MATCH_ALL;
        rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
        rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
        rule.Redirect.Target = Sock;
    } else {
        rule.Match = XDP_MATCH_ALL;

        switch (RandUlong() % 4) {
        case 0:
            rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
            rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
            rule.Redirect.Target = Sock;
            break;

        case 1:
            rule.Action = XDP_PROGRAM_ACTION_L2FWD;
            break;

        case 2:
            rule.Action = XDP_PROGRAM_ACTION_EBPF;
            rule.Ebpf.Target =
                (HANDLE)((((UINT64)(RandUlong() & 0x3)) << 32) | (RandUlong() & 0x3));
            break;

        case 3:
            return AttachXdpEbpfProgram(Queue, Sock, RxProgramSet);
        }
    }

    if (Queue->xdpMode == XdpModeGeneric) {
        flags |= XDP_CREATE_PROGRAM_FLAG_GENERIC;
    } else if (Queue->xdpMode == XdpModeNative) {
        flags |= XDP_CREATE_PROGRAM_FLAG_NATIVE;
    }

    if (RandUlong() % 8) {
        flags |= XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES;
    }

    res = Queue->xdpApi->XskGetSockopt(Sock, XSK_SOCKOPT_RX_HOOK_ID, &hookId, &hookIdSize);
    if (FAILED(res)) {
        goto Exit;
    }
    ASSERT_FRE(hookIdSize == sizeof(hookId));

    FuzzHookId(&hookId);

    if (!(RandUlong() % 32)) {
        PortSet = malloc(XDP_PORT_SET_BUFFER_SIZE);
        if (PortSet == NULL) {
            res = E_OUTOFMEMORY;
            goto Exit;
        }

        switch (RandUlong() % 5) {
        case 0:
            rule.Match = XDP_MATCH_UDP_PORT_SET;
            rule.Pattern.PortSet.PortSet = RandUlong() % 4 ? PortSet : NULL;
            break;
        case 1:
            rule.Match = XDP_MATCH_IPV4_UDP_PORT_SET;
            rule.Pattern.IpPortSet.PortSet.PortSet = RandUlong() % 4 ? PortSet : NULL;
            break;
        case 2:
            rule.Match = XDP_MATCH_IPV6_UDP_PORT_SET;
            rule.Pattern.IpPortSet.PortSet.PortSet = RandUlong() % 4 ? PortSet : NULL;
            break;
        case 3:
            rule.Match = XDP_MATCH_IPV4_TCP_PORT_SET;
            rule.Pattern.IpPortSet.PortSet.PortSet = RandUlong() % 4 ? PortSet : NULL;
            break;
        case 4:
            rule.Match = XDP_MATCH_IPV6_TCP_PORT_SET;
            rule.Pattern.IpPortSet.PortSet.PortSet = RandUlong() % 4 ? PortSet : NULL;
        }
    }

    res =
        Queue->xdpApi->XdpCreateProgram(
            ifindex, &hookId, Queue->queueId, flags, &rule, 1, &handle);
    if (SUCCEEDED(res)) {
        EnterCriticalSection(&RxProgramSet->Lock);
        if (RxProgramSet->HandleCount < RTL_NUMBER_OF(RxProgramSet->Handles)) {
            RxProgramSet->Handles[RxProgramSet->HandleCount].Type = ProgramHandleXdp;
            RxProgramSet->Handles[RxProgramSet->HandleCount].Handle = handle;
            RxProgramSet->HandleCount++;
        } else {
            ASSERT_FRE(CloseHandle(handle));
        }
        LeaveCriticalSection(&RxProgramSet->Lock);
    }

Exit:

    if (PortSet != NULL) {
        free(PortSet);
    }

    return RxRequired ? res : S_OK;
}

VOID
DetachXdpProgram(
    _Inout_ XSK_PROGRAM_SET *RxProgramSet
    )
{
    HANDLE Handle = NULL;
    struct bpf_object *BpfObject = NULL;

    EnterCriticalSection(&RxProgramSet->Lock);
    if (RxProgramSet->HandleCount > 0) {
        UINT32 detachIndex = RandUlong() % RxProgramSet->HandleCount;

        switch (RxProgramSet->Handles[detachIndex].Type) {
        case ProgramHandleXdp:
            Handle = RxProgramSet->Handles[detachIndex].Handle;
            break;

        case ProgramHandleEbpf:
            BpfObject = RxProgramSet->Handles[detachIndex].BpfObject;
            break;

        default:
            ASSERT_FRE(FALSE);
        }
        RxProgramSet->Handles[detachIndex] = RxProgramSet->Handles[--RxProgramSet->HandleCount];
    }
    LeaveCriticalSection(&RxProgramSet->Lock);

    if (Handle != NULL) {
        ASSERT_FRE(CloseHandle(Handle));
    }

    if (BpfObject != NULL) {
        int OriginalThreadPriority = GetThreadPriority(GetCurrentThread());
        ASSERT_FRE(OriginalThreadPriority != THREAD_PRIORITY_ERROR_RETURN);

        //
        // To work around control path delays caused by eBPF's epoch implementation,
        // boost this thread's priority when invoking eBPF APIs.
        //
        ASSERT_FRE(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST));

        TraceVerbose("bpf_object__close(%p)", BpfObject);
        bpf_object__close(BpfObject);

        ASSERT_FRE(SetThreadPriority(GetCurrentThread(), OriginalThreadPriority));
    }
}

HRESULT
FreeRingInitialize(
    _Inout_ XSK_RING *FreeRing,
    _Out_ BYTE **FreeRingBase,
    _In_ UINT32 DescriptorStart,
    _In_ UINT32 DescriptorStride,
    _In_ UINT32 DescriptorCount
    )
{
    XSK_RING_INFO freeRingInfo = {0};
    UINT64 desc = DescriptorStart;

    struct {
        UINT32 Producer;
        UINT32 Consumer;
        UINT32 Flags;
        UINT64 Descriptors[0];
    } *FreeRingLayout;

    FreeRingLayout =
        calloc(1, sizeof(*FreeRingLayout) + DescriptorCount * sizeof(*FreeRingLayout->Descriptors));
    if (FreeRingLayout == NULL) {
        return E_OUTOFMEMORY;
    }
    *FreeRingBase = (BYTE *)FreeRingLayout;

    freeRingInfo.Ring = (BYTE *)FreeRingLayout;
    freeRingInfo.ProducerIndexOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Producer);
    freeRingInfo.ConsumerIndexOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Consumer);
    freeRingInfo.FlagsOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Flags);
    freeRingInfo.DescriptorsOffset = (UINT32)STRUCT_FIELD_OFFSET(FreeRingLayout, Descriptors[0]);
    freeRingInfo.Size = DescriptorCount;
    freeRingInfo.ElementStride = sizeof(*FreeRingLayout->Descriptors);
    XskRingInitialize(FreeRing, &freeRingInfo);

    for (UINT32 i = 0; i < DescriptorCount; i++) {
        UINT64 *Descriptor = XskRingGetElement(FreeRing, i);
        *Descriptor = desc;
        desc += DescriptorStride;
    }
    XskRingProducerSubmit(FreeRing, DescriptorCount);

    return S_OK;
}

VOID
CleanupQueue(
    _In_ QUEUE_CONTEXT *Queue
    )
{
    BOOL res;

    while (Queue->rxProgramSet.HandleCount > 0) {
        DetachXdpProgram(&Queue->rxProgramSet);
    }

    while (Queue->sharedUmemRxProgramSet.HandleCount > 0) {
        DetachXdpProgram(&Queue->sharedUmemRxProgramSet);
    }

    if (Queue->sock != NULL) {
        ASSERT_FRE(CloseHandle(Queue->sock));
    }
    if (Queue->sharedUmemSock != NULL) {
        ASSERT_FRE(CloseHandle(Queue->sharedUmemSock));
    }
    if (Queue->rss != NULL) {
        ASSERT_FRE(CloseHandle(Queue->rss));
    }

    if (Queue->fuzzerShared.pauseEvent != NULL) {
        ASSERT_FRE(CloseHandle(Queue->fuzzerShared.pauseEvent));
    }
    if (Queue->scenarioConfig.completeEvent != NULL) {
        ASSERT_FRE(CloseHandle(Queue->scenarioConfig.completeEvent));
    }

    if (Queue->fuzzers != NULL) {
        free(Queue->fuzzers);
    }

    if (Queue->umemReg.Address != NULL) {
        res = VirtualFree(Queue->umemReg.Address, 0, MEM_RELEASE);
        ASSERT_FRE(res);
    }

    if (Queue->xdpApi != NULL) {
        XdpCloseApi(Queue->xdpApi);
    }

    DeleteCriticalSection(&Queue->sharedUmemRxProgramSet.Lock);
    DeleteCriticalSection(&Queue->rxProgramSet.Lock);

    free(Queue);
}

VOID
FuzzRssSet(
    _In_ XSK_FUZZER_WORKER *fuzzer,
    _In_ QUEUE_CONTEXT *queue
    )
{
    XDP_RSS_CONFIGURATION* RssConfiguration = NULL;
    UINT8 *ProcessorGroups = NULL;

    if (queue->XdpRssSet == NULL) {
        goto Exit;
    }

    WORD ActiveProcGroups = GetActiveProcessorGroupCount();
    if (ActiveProcGroups == 0) {
        goto Exit;
    }

    ProcessorGroups = malloc(sizeof(UINT8) * ActiveProcGroups);
    if (ProcessorGroups == NULL) {
        goto Exit;
    }

    DWORD ActualNumProcessors = 0;
    for (WORD i = 0; i < ActiveProcGroups; i++) {
        UINT8 NumProcsInGroup = (UINT8)GetActiveProcessorCount(i);
        ProcessorGroups[i] = NumProcsInGroup;
        ActualNumProcessors += NumProcsInGroup;
    }

    UINT32 NumProcessors = (RandUlong() % ActualNumProcessors) + 1;

    UINT16 HashSecretKeySize = RandUlong() % 41;
    UINT32 IndirectionTableSize = NumProcessors * sizeof(PROCESSOR_NUMBER);
    UINT32 RssConfigSize =
        sizeof(XDP_RSS_CONFIGURATION) +
        HashSecretKeySize +
        IndirectionTableSize;

    RssConfiguration = malloc(RssConfigSize);
    if (RssConfiguration == NULL) {
        goto Exit;
    }
    RtlZeroMemory(RssConfiguration, RssConfigSize);
    RssConfiguration->Header.Revision = XDP_RSS_CONFIGURATION_REVISION_1;
    RssConfiguration->Header.Size = XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1;
    RssConfiguration->HashSecretKeyOffset = (RandUlong() % 64 == 0) ? (UINT16)RandUlong() : sizeof(XDP_RSS_CONFIGURATION);
    UINT16 ActualIndirectionTableOffset =  sizeof(XDP_RSS_CONFIGURATION) + HashSecretKeySize;
    RssConfiguration->IndirectionTableOffset = (RandUlong() % 64 == 0) ? (UINT16)RandUlong() : ActualIndirectionTableOffset;

    PROCESSOR_NUMBER *IndirectionTableDst =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfiguration, ActualIndirectionTableOffset);

    for (UINT32 ProcNumIndex = 0; ProcNumIndex < NumProcessors; ProcNumIndex++) {
        UINT16 SelectedGroup = RandUlong() % ActiveProcGroups;
        UINT8 SelectedProcNumber = RandUlong() % ProcessorGroups[SelectedGroup];

        IndirectionTableDst[ProcNumIndex].Group = SelectedGroup;
        IndirectionTableDst[ProcNumIndex].Number = SelectedProcNumber;
    }

    RssConfiguration->Flags = (RandUlong() % 64 == 0) ? RandUlong() :
        XDP_RSS_FLAG_SET_HASH_TYPE | XDP_RSS_FLAG_SET_HASH_SECRET_KEY |
        XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    RssConfiguration->HashType = (RandUlong() % 64 == 0) ? RandUlong() : XDP_RSS_VALID_HASH_TYPES;
    RssConfiguration->HashSecretKeySize = (RandUlong() % 64 == 0) ? (UINT16)RandUlong() : HashSecretKeySize;
    RssConfiguration->IndirectionTableSize = (RandUlong() % 64 == 0) ? (UINT16)RandUlong() : (USHORT)IndirectionTableSize;

    //
    // It's OK if XdpRssSet fails, as spin is to make sure random inputs don't
    // crash the system.
    //

    if (fuzzer->fuzzerInterface != NULL) {
        (void)queue->XdpRssSet(fuzzer->fuzzerInterface, RssConfiguration, RssConfigSize);
    } else {
        AcquireSRWLockShared(&queue->rssLock);
        if (queue->rss != NULL) {
            (void)queue->XdpRssSet(queue->rss, RssConfiguration, RssConfigSize);
        }
        ReleaseSRWLockShared(&queue->rssLock);
    }

Exit:

    if (RssConfiguration != NULL) {
        free(RssConfiguration);
    }
    if (ProcessorGroups != NULL) {
        free(ProcessorGroups);
    }
}

VOID
FuzzRssGet(
    _In_ XSK_FUZZER_WORKER *Fuzzer,
    _In_ QUEUE_CONTEXT *Queue
    )
{
    XDP_RSS_CONFIGURATION* rssConfiguration = NULL;
    HRESULT res;
    UINT32 size = 0;

    if (Queue->XdpRssGet == NULL) {
        goto Exit;
    }

    if (Fuzzer->fuzzerInterface != NULL) {
        res = Queue->XdpRssGet(Fuzzer->fuzzerInterface, NULL, &size);
    } else {
        AcquireSRWLockShared(&Queue->rssLock);
        if (Queue->rss != NULL) {
            res = Queue->XdpRssGet(Queue->rss, NULL, &size);
        } else {
            res = E_INVALIDARG;
        }
        ReleaseSRWLockShared(&Queue->rssLock);
    }

    if (res != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
        goto Exit;
    }

    rssConfiguration = malloc((UINT64)size * 2);
    if (rssConfiguration == NULL) {
        goto Exit;
    }

    size = RandUlong() % (size * 2);

    if (Fuzzer->fuzzerInterface != NULL) {
#pragma prefast(suppress : 6386, "SAL does not understand the mod operator")
        Queue->XdpRssGet(Fuzzer->fuzzerInterface, rssConfiguration, &size);
    } else {
        AcquireSRWLockShared(&Queue->rssLock);
        if (Queue->rss != NULL) {
#pragma prefast(suppress : 6386, "SAL does not understand the mod operator")
            Queue->XdpRssGet(Queue->rss, rssConfiguration, &size);
        }
        ReleaseSRWLockShared(&Queue->rssLock);
    }

Exit:

    if (rssConfiguration != NULL) {
        free(rssConfiguration);
    }
}

VOID
FuzzRssGetCapabilities(
    _In_ XSK_FUZZER_WORKER *Fuzzer,
    _In_ QUEUE_CONTEXT *Queue
    )
{
    XDP_RSS_CAPABILITIES *rssCapabilities = NULL;
    HRESULT res;
    UINT32 size = 0;

    if (Queue->XdpRssGetCapabilities == NULL) {
        goto Exit;
    }

    if (Fuzzer->fuzzerInterface != NULL) {
        res = Queue->XdpRssGetCapabilities(Fuzzer->fuzzerInterface, NULL, &size);
    } else {
        AcquireSRWLockShared(&Queue->rssLock);
        if (Queue->rss != NULL) {
            res = Queue->XdpRssGetCapabilities(Queue->rss, NULL, &size);
        } else {
            res = E_INVALIDARG;
        }
        ReleaseSRWLockShared(&Queue->rssLock);
    }

    if (res != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
        goto Exit;
    }

    rssCapabilities = malloc((UINT64)size * 2);
    if (rssCapabilities == NULL) {
        goto Exit;
    }

    size = RandUlong() % (size * 2);

    if (Fuzzer->fuzzerInterface != NULL) {
#pragma prefast(suppress : 6386, "SAL does not understand the mod operator")
        Queue->XdpRssGetCapabilities(Fuzzer->fuzzerInterface, rssCapabilities, &size);
    } else {
        AcquireSRWLockShared(&Queue->rssLock);
        if (Queue->rss != NULL) {
#pragma prefast(suppress : 6386, "SAL does not understand the mod operator")
            Queue->XdpRssGetCapabilities(Queue->rss, rssCapabilities, &size);
        }
        ReleaseSRWLockShared(&Queue->rssLock);
    }

Exit:

    if (rssCapabilities != NULL) {
        free(rssCapabilities);
    }
}

VOID
FuzzQeoSet(
    _In_ XSK_FUZZER_WORKER *fuzzer,
    _In_ QUEUE_CONTEXT *queue
    )
{
    XDP_QUIC_CONNECTION *Connections = NULL;
    UINT32 ConnectionCount = RandUlong() % 16;
    UINT32 ConnectionsSize = ConnectionCount * sizeof(*Connections);

    if (queue->XdpQeoSet == NULL) {
        goto Exit;
    }

    Connections = calloc(1, ConnectionsSize);
    if (Connections == NULL) {
        goto Exit;
    }

    for (UINT32 i = 0; i < ConnectionCount; i++) {
        XDP_QUIC_CONNECTION *Connection = &Connections[i];

        XdpInitializeQuicConnection(Connection, sizeof(*Connection));
        Connection->Operation = RandUlong() % 4;
        Connection->Direction = RandUlong() % 4;
        Connection->DecryptFailureAction = RandUlong() % 4;
        Connection->KeyPhase = RandUlong() % 4;
        Connection->CipherType = RandUlong() % 8;
        Connection->AddressFamily = RandUlong() % 4;
        Connection->UdpPort = (UINT16)RandUlong();
        Connection->NextPacketNumber = RandUlong();
        Connection->ConnectionIdLength = RandUlong() % (sizeof(Connection->ConnectionId) + 4);
    }

    if (fuzzer->fuzzerInterface != NULL) {
        (void)queue->XdpQeoSet(fuzzer->fuzzerInterface, Connections, ConnectionsSize);
    }

Exit:

    if (Connections != NULL) {
        free(Connections);
    }
}

VOID
FuzzInterface(
    _In_ XSK_FUZZER_WORKER *fuzzer,
    _In_ QUEUE_CONTEXT *queue
    )
{
    if (RandUlong() % 50 == 0) {
        // Close Local
        if (fuzzer->fuzzerInterface) {
            ASSERT_FRE(CloseHandle(fuzzer->fuzzerInterface));
            fuzzer->fuzzerInterface = NULL;
        }
    }

    if (RandUlong() % 50 == 0) {
        // Close Shared
        HANDLE sharedToClose = NULL;
        AcquireSRWLockExclusive(&queue->rssLock);
        sharedToClose = queue->rss;
        queue->rss = NULL;
        ReleaseSRWLockExclusive(&queue->rssLock);

        if (sharedToClose != NULL) {
            ASSERT_FRE(CloseHandle(sharedToClose));
            sharedToClose = NULL;
        }
    }

    if (RandUlong() % 10 == 0) {
        // Open Local
        if (fuzzer->fuzzerInterface) {
            ASSERT_FRE(CloseHandle(fuzzer->fuzzerInterface));
            fuzzer->fuzzerInterface = NULL;
        }

        queue->xdpApi->XdpInterfaceOpen(ifindex, &fuzzer->fuzzerInterface);
    }

    if (RandUlong() % 25 == 0) {
        // swap local and shared
        AcquireSRWLockExclusive(&queue->rssLock);
        HANDLE tmp = queue->rss;
        queue->rss = fuzzer->fuzzerInterface;
        fuzzer->fuzzerInterface = tmp;
        ReleaseSRWLockExclusive(&queue->rssLock);
    }

    if (RandUlong() % 10 == 0) {
        FuzzRssSet(fuzzer, queue);
    }

    if (RandUlong() % 10 == 0) {
        FuzzRssGet(fuzzer, queue);
    }

    if (RandUlong() % 10 == 0) {
        FuzzRssGetCapabilities(fuzzer, queue);
    }

    if (RandUlong() % 10 == 0) {
        FuzzQeoSet(fuzzer, queue);
    }
}

HRESULT
InitializeQueue(
    _In_ UINT32 QueueId,
    _Out_ QUEUE_CONTEXT **Queue
    )
{
    HRESULT res;
    QUEUE_CONTEXT *queue;

    queue = malloc(sizeof(*queue));
    if (queue == NULL) {
        res = E_OUTOFMEMORY;
        goto Exit;
    }
    RtlZeroMemory(queue, sizeof(*queue));

    queue->queueId = QueueId;
    queue->fuzzerCount = fuzzerCount;
    InitializeCriticalSection(&queue->rxProgramSet.Lock);
    InitializeCriticalSection(&queue->sharedUmemRxProgramSet.Lock);
    InitializeSRWLock(&queue->rssLock);

    res = XdpOpenApi(XDP_API_VERSION_1, &queue->xdpApi);
    if (FAILED(res)) {
        goto Exit;
    }

    queue->XdpRssGetCapabilities =
        (XDP_RSS_GET_CAPABILITIES_FN *)queue->xdpApi->XdpGetRoutine(
            XDP_RSS_GET_CAPABILITIES_FN_NAME);
    queue->XdpRssSet = (XDP_RSS_SET_FN *)queue->xdpApi->XdpGetRoutine(XDP_RSS_SET_FN_NAME);
    queue->XdpRssGet = (XDP_RSS_GET_FN *)queue->xdpApi->XdpGetRoutine(XDP_RSS_GET_FN_NAME);
    queue->XdpQeoSet = (XDP_QEO_SET_FN *)queue->xdpApi->XdpGetRoutine(XDP_QEO_SET_FN_NAME);

    queue->fuzzers = calloc(queue->fuzzerCount, sizeof(*queue->fuzzers));
    if (queue->fuzzers == NULL) {
        res = E_OUTOFMEMORY;
        goto Exit;
    }

    queue->fuzzerShared.pauseEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ASSERT_FRE(queue->fuzzerShared.pauseEvent != NULL);
    queue->scenarioConfig.completeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ASSERT_FRE(queue->scenarioConfig.completeEvent != NULL);
    queue->fuzzerShared.queue = queue;
    for (UINT32 i = 0; i < queue->fuzzerCount; i++) {
        queue->fuzzers[i].shared = &queue->fuzzerShared;
    }

    switch (RandUlong() % 3) {
    case 0:
        queue->xdpMode = XdpModeSystem;
        break;
    case 1:
        queue->xdpMode = XdpModeGeneric;
        break;
    case 2:
        queue->xdpMode = XdpModeNative;
        break;
    }

    //
    // Fuzz the IO scenario.
    //
    switch (RandUlong() % 3) {
    case 0:
        queue->scenarioConfig.sockRx = TRUE;
        break;
    case 1:
        queue->scenarioConfig.sockTx = TRUE;
        break;
    case 2:
        queue->scenarioConfig.sockRx = TRUE;
        queue->scenarioConfig.sockTx = TRUE;
        if (RandUlong() % 2) {
            queue->scenarioConfig.sockRxTxSeparateThreads = TRUE;
        }
        break;
    }
    if (!queue->scenarioConfig.sockRx && (RandUlong() % 2)) {
        queue->scenarioConfig.sharedUmemSockRx = TRUE;
    }
    if (RandUlong() % 2) {
        queue->scenarioConfig.sharedUmemSockTx = TRUE;
    }

    res = queue->xdpApi->XskCreate(&queue->sock);
    if (FAILED(res)) {
        goto Exit;
    }

    if (queue->scenarioConfig.sharedUmemSockRx || queue->scenarioConfig.sharedUmemSockTx) {
        res = queue->xdpApi->XskCreate(&queue->sharedUmemSock);
        if (FAILED(res)) {
            goto Exit;
        }
    }

    queue->datapathShared.queue = queue;
    queue->datapath1.shared = &queue->datapathShared;
    queue->datapath2.shared = &queue->datapathShared;

    queue->datapath1.sock = queue->sock;
    queue->datapath1.rxProgramSet = &queue->rxProgramSet;
    if (queue->scenarioConfig.sockRxTxSeparateThreads) {
        queue->datapath1.flags.rx = TRUE;
        queue->datapath2.sock = queue->sock;
        queue->datapath2.rxProgramSet = &queue->rxProgramSet;
        queue->datapath2.flags.tx = TRUE;
    } else {
        if (queue->scenarioConfig.sockRx) {
            queue->datapath1.flags.rx = TRUE;
        }
        if (queue->scenarioConfig.sockTx) {
            queue->datapath1.flags.tx = TRUE;
        }
        if (queue->scenarioConfig.sharedUmemSockRx) {
            queue->datapath2.sock = queue->sharedUmemSock;
            queue->datapath2.rxProgramSet = &queue->sharedUmemRxProgramSet;
            queue->datapath2.flags.rx = TRUE;
        }
        if (queue->scenarioConfig.sharedUmemSockTx) {
            queue->datapath2.sock = queue->sharedUmemSock;
            queue->datapath2.rxProgramSet = &queue->sharedUmemRxProgramSet;
            queue->datapath2.flags.tx = TRUE;
        }
    }
    TraceVerbose(
        "q[%u]: datapath1_rx:%d datapath1_tx:%d datapath2_rx:%d datapath2_tx:%d sharedUmem:%d xdpMode:%s",
        queue->queueId, queue->datapath1.flags.rx, queue->datapath1.flags.tx,
        queue->datapath2.flags.rx, queue->datapath2.flags.tx,
        (queue->datapath1.sock != queue->datapath2.sock && queue->datapath2.sock != NULL),
        XdpModeToString[queue->xdpMode]);

    *Queue = queue;
    ASSERT_FRE(SUCCEEDED(res));

Exit:

    if (FAILED(res)) {
        if (queue != NULL) {
            CleanupQueue(queue);
        }
    }

    return res;
}

VOID
FuzzSocketUmemSetup(
    _Inout_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _Inout_ BOOLEAN *WasUmemRegistered
    )
{
    HRESULT res;

    if (RandUlong() % 2) {
        XSK_UMEM_REG umemReg = {0};

        //
        // Limit the total UMEM size to avoid trashing the system - some non-XDP
        // system components (e.g. ETW) are sensitive to out-of-memory
        // conditions, and attempting to lock pages can be very slow. Using
        // driver verifier's fault injection provides coverage of mapping
        // failure paths.
        //
        umemReg.TotalSize = RandUlong() % 0x100000;

        if (RandUlong() % 6) {
            umemReg.ChunkSize = RandUlong() % 4096;
        } else {
            umemReg.ChunkSize = RandUlong();
        }

        if (RandUlong() % 6) {
            umemReg.Headroom = RandUlong() % ((umemReg.ChunkSize / 4) + 1);
        } else {
            umemReg.Headroom = RandUlong();
        }

        umemReg.Address =
            VirtualAlloc(
                NULL, umemReg.TotalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (umemReg.Address == NULL) {
            return;
        }

        res =
            Queue->xdpApi->XskSetSockopt(
                Sock, XSK_SOCKOPT_UMEM_REG, &umemReg, sizeof(umemReg));
        if (SUCCEEDED(res)) {
            Queue->umemReg = umemReg;
            WriteBooleanRelease(WasUmemRegistered, TRUE);
            TraceVerbose("q[%u]: umem totalSize:%llu chunkSize:%u headroom:%u",
                Queue->queueId, umemReg.TotalSize, umemReg.ChunkSize, umemReg.Headroom);
        } else {
            BOOL success = VirtualFree(umemReg.Address, 0, MEM_RELEASE);
            ASSERT_FRE(success);
        }
    }
}

VOID
FuzzSocketSharedUmemSetup(
    _Inout_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _In_ HANDLE SharedUmemSock
    )
{
    HRESULT res;

    if (RandUlong() % 2) {
        res =
            Queue->xdpApi->XskSetSockopt(
                Sock, XSK_SOCKOPT_UMEM_REG, &Queue->umemReg, sizeof(Queue->umemReg));
        if (SUCCEEDED(res)) {
            TraceVerbose(
                "q[%u]: umem shared with SharedUmemSock=%p Buffer=%p",
                Queue->queueId, SharedUmemSock, Queue->umemReg.Address);
        }
    }
}

VOID
FuzzRingSize(
    _In_ QUEUE_CONTEXT *Queue,
    _Out_ UINT32 *Size
    )
{
    UINT32 numUmemDescriptors;

    if (ReadBooleanAcquire(&Queue->scenarioConfig.isUmemRegistered)) {
        numUmemDescriptors = (UINT32)(Queue->umemReg.TotalSize / Queue->umemReg.ChunkSize);
    } else {
        numUmemDescriptors = (RandUlong() % 16) + 1;
    }

    if (RandUlong() % 2) {
        *Size = 1ui32 << (RandUlong() % (RtlFindMostSignificantBit(numUmemDescriptors) + 1));
    } else {
        *Size = RandUlong();
    }
}

VOID
FuzzSocketRxTxSetup(
    _In_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _In_ BOOLEAN RequiresRx,
    _In_ BOOLEAN RequiresTx,
    _Inout_ BOOLEAN *WasRxSet,
    _Inout_ BOOLEAN *WasTxSet
    )
{
    HRESULT res;
    UINT32 ringSize;

    if (RequiresRx) {
        if (RandUlong() % 2) {
            FuzzRingSize(Queue, &ringSize);
            res =
                Queue->xdpApi->XskSetSockopt(
                    Sock, XSK_SOCKOPT_RX_RING_SIZE, &ringSize, sizeof(ringSize));
            if (SUCCEEDED(res)) {
                WriteBooleanRelease(WasRxSet, TRUE);
            }
        }
    }

    if (RequiresTx) {
        if (RandUlong() % 2) {
            FuzzRingSize(Queue, &ringSize);
            res =
                Queue->xdpApi->XskSetSockopt(
                    Sock, XSK_SOCKOPT_TX_RING_SIZE, &ringSize, sizeof(ringSize));
            if (SUCCEEDED(res)) {
                WriteBooleanRelease(WasTxSet, TRUE);
            }
        }
    }

    if (RandUlong() % 2) {
        FuzzRingSize(Queue, &ringSize);
        res =
            Queue->xdpApi->XskSetSockopt(
                Sock, XSK_SOCKOPT_RX_FILL_RING_SIZE, &ringSize, sizeof(ringSize));
    }

    if (RandUlong() % 2) {
        FuzzRingSize(Queue, &ringSize);
        res =
            Queue->xdpApi->XskSetSockopt(
                Sock, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &ringSize, sizeof(ringSize));
    }
}

VOID
FuzzSocketMisc(
    _In_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _Inout_ XSK_PROGRAM_SET *RxProgramSet
    )
{
    UINT32 optSize;

    //
    // For simplicity, use a single shared overlapped structure for fuzzed IO.
    // This overlapped structure will be used for several concurrent IOs, which
    // is usually unadvisable, but since we do not care about the results of the
    // IOs, this is fine.
    //
    static OVERLAPPED overlapped = {0};

    if (RandUlong() % 2) {
        XSK_RING_INFO_SET ringInfo;
        optSize = sizeof(ringInfo);
        Queue->xdpApi->XskGetSockopt(Sock, XSK_SOCKOPT_RING_INFO, &ringInfo, &optSize);
    }

    if (RandUlong() % 2) {
        XSK_STATISTICS stats;
        optSize = sizeof(stats);
        Queue->xdpApi->XskGetSockopt(Sock, XSK_SOCKOPT_STATISTICS, &stats, &optSize);
    }

    if (RandUlong() % 2) {
        XDP_HOOK_ID hookId = {
            XDP_HOOK_L2,
            XDP_HOOK_RX,
            XDP_HOOK_INSPECT,
        };
        FuzzHookId(&hookId);
        Queue->xdpApi->XskSetSockopt(Sock, XSK_SOCKOPT_RX_HOOK_ID, &hookId, sizeof(hookId));
    }

    if (RandUlong() % 2) {
        XDP_HOOK_ID hookId = {
            XDP_HOOK_L2,
            XDP_HOOK_TX,
            XDP_HOOK_INJECT,
        };
        FuzzHookId(&hookId);
        Queue->xdpApi->XskSetSockopt(Sock, XSK_SOCKOPT_TX_HOOK_ID, &hookId, sizeof(hookId));
    }

    if (RandUlong() % 2) {
        XSK_NOTIFY_FLAGS notifyFlags = XSK_NOTIFY_FLAG_NONE;
        UINT32 timeoutMs = 0;
        XSK_NOTIFY_RESULT_FLAGS notifyResult;

        if (RandUlong() % 2) {
            notifyFlags |= XSK_NOTIFY_FLAG_POKE_RX;
        }
        if (RandUlong() % 2) {
            notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
        }
        if (RandUlong() % 2) {
            notifyFlags |= XSK_NOTIFY_FLAG_WAIT_RX;
        }
        if (RandUlong() % 2) {
            notifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
        }

        if (RandUlong() % 2) {
            timeoutMs = RandUlong() % 1000;
        }

        if (RandUlong() % 2) {
            Queue->xdpApi->XskNotifySocket(Sock, notifyFlags, timeoutMs, &notifyResult);
        } else {
            Queue->xdpApi->XskNotifyAsync(Sock, notifyFlags, &overlapped);
        }
    }

    if (!(RandUlong() % 8)) {
        CancelIoEx(Sock, &overlapped);
    }

    if (RandUlong() % 2) {
        PROCESSOR_NUMBER procNum;
        PROCESSOR_NUMBER *procNumParam = &procNum;
        optSize = sizeof(procNum);
        UINT32 option;

        if (RandUlong() % 2) {
            option = XSK_SOCKOPT_RX_PROCESSOR_AFFINITY;
        } else {
            option = XSK_SOCKOPT_TX_PROCESSOR_AFFINITY;
        }

        if (RandUlong() % 4) {
            procNumParam = NULL;
        }

        if (RandUlong() % 4) {
            optSize = RandUlong() % 21;
        }

        #pragma prefast(suppress:6387) // Intentionally passing NULL parameter.
        Queue->xdpApi->XskGetSockopt(Sock, option, procNumParam, &optSize);
    }

    if (!cleanDatapath && !(RandUlong() % 3)) {
        DetachXdpProgram(RxProgramSet);

        if (RxProgramSet->HandleCount == 0) {
            WaitForSingleObject(stopEvent, 20);
            AttachXdpProgram(Queue, Sock, FALSE, RxProgramSet);
        }
    }

    if (!cleanDatapath && !(RandUlong() % 3)) {
        AttachXdpProgram(Queue, Sock, FALSE, RxProgramSet);
    }
}

VOID
FuzzSocketBind(
    _In_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx,
    _Inout_ BOOLEAN *WasSockBound
    )
{
    HRESULT res;
    UINT32 bindFlags = 0;

    if (RandUlong() % 2) {
        if (Rx) {
            bindFlags |= XSK_BIND_FLAG_RX;
        }

        if (Tx) {
            bindFlags |= XSK_BIND_FLAG_TX;
        }

        if (Queue->xdpMode == XdpModeGeneric) {
            bindFlags |= XSK_BIND_FLAG_GENERIC;
        } else if (Queue->xdpMode == XdpModeNative) {
            bindFlags |= XSK_BIND_FLAG_NATIVE;
        }

        if (!(RandUlong() % 10)) {
            bindFlags |= 0x1 << (RandUlong() % 32);
        }

        res = Queue->xdpApi->XskBind(Sock, ifindex, Queue->queueId, bindFlags);
        if (SUCCEEDED(res)) {
            WriteBooleanRelease(WasSockBound, TRUE);
        }
    }
}

VOID
FuzzSocketActivate(
    _In_ QUEUE_CONTEXT *Queue,
    _In_ HANDLE Sock,
    _Inout_ BOOLEAN *WasSockActivated
    )
{
    HRESULT res;
    UINT32 activateFlags = 0;

    if (RandUlong() % 2) {
        if (!(RandUlong() % 10)) {
            activateFlags = RandUlong() & RandUlong();
        }

        res = Queue->xdpApi->XskActivate(Sock, activateFlags);
        if (SUCCEEDED(res)) {
            WriteBooleanRelease(WasSockActivated, TRUE);
        }
    }
}

VOID
CleanupDatapath(
    _Inout_ XSK_DATAPATH_WORKER *Datapath
    )
{
    if (Datapath->rxFreeRingBase) {
        free(Datapath->rxFreeRingBase);
    }
    if (Datapath->txFreeRingBase) {
        free(Datapath->txFreeRingBase);
    }
}

HRESULT
InitializeDatapath(
    _Inout_ XSK_DATAPATH_WORKER *Datapath
    )
{
    HRESULT res;
    XSK_RING_INFO_SET ringInfo;
    UINT32 ringInfoSize = sizeof(ringInfo);
    QUEUE_CONTEXT *queue = Datapath->shared->queue;
    UINT32 umemDescriptorCount =
        (UINT32)(queue->umemReg.TotalSize / queue->umemReg.ChunkSize);
    UINT32 descriptorCount = umemDescriptorCount / 2;
    UINT32 descriptorStart;

    Datapath->iobatchsize = DEFAULT_IO_BATCH;
    Datapath->pollMode = XSK_POLL_MODE_DEFAULT;
    Datapath->flags.wait = FALSE;
    Datapath->txiosize = queue->umemReg.ChunkSize - queue->umemReg.Headroom;

    res =
        queue->xdpApi->XskGetSockopt(
            Datapath->sock, XSK_SOCKOPT_RING_INFO, &ringInfo, &ringInfoSize);
    if (FAILED(res)) {
        goto Exit;
    }
    ASSERT_FRE(ringInfoSize == sizeof(ringInfo));

    //
    // Create separate free rings for RX and TX, each getting half of the UMEM
    // descriptor space.
    //

    if (Datapath->flags.rx) {
        TraceVerbose("q[%u]d[0x%p]: rx_size:%u fill_size:%u",
            queue->queueId, Datapath->threadHandle, ringInfo.Rx.Size, ringInfo.Fill.Size);
        XskRingInitialize(&Datapath->rxRing, &ringInfo.Rx);
        XskRingInitialize(&Datapath->fillRing, &ringInfo.Fill);

        ASSERT_FRE(Datapath->rxRing.Size > 0 && Datapath->fillRing.Size > 0);

        descriptorStart = 0;
        res =
            FreeRingInitialize(
                &Datapath->rxFreeRing, &Datapath->rxFreeRingBase, descriptorStart,
                queue->umemReg.ChunkSize, descriptorCount);
        if (FAILED(res)) {
            goto Exit;
        }

    }
    if (Datapath->flags.tx) {
        TraceVerbose("q[%u]d[0x%p]: tx_size:%u comp_size:%u",
            queue->queueId, Datapath->threadHandle, ringInfo.Tx.Size, ringInfo.Completion.Size);
        XskRingInitialize(&Datapath->txRing, &ringInfo.Tx);
        XskRingInitialize(&Datapath->compRing, &ringInfo.Completion);

        ASSERT_FRE(Datapath->txRing.Size > 0 && Datapath->compRing.Size > 0);

        descriptorStart = (umemDescriptorCount / 2) * queue->umemReg.ChunkSize;
        res =
            FreeRingInitialize(
                &Datapath->txFreeRing, &Datapath->txFreeRingBase, descriptorStart,
                queue->umemReg.ChunkSize, descriptorCount);
        if (FAILED(res)) {
            goto Exit;
        }
    }

    res =
        AttachXdpProgram(
            Datapath->shared->queue, Datapath->sock, Datapath->flags.rx, Datapath->rxProgramSet);
    if (FAILED(res)) {
        goto Exit;
    }

    ASSERT_FRE(SUCCEEDED(res));

Exit:

    if (FAILED(res)) {
        CleanupDatapath(Datapath);
    }

    return res;
}

VOID
NotifyDriver(
    _In_ XSK_DATAPATH_WORKER *Datapath,
    _In_ XSK_NOTIFY_FLAGS DirectionFlags
    )
{
    XSK_NOTIFY_RESULT_FLAGS notifyResult;

    //
    // Ensure poke flags are read after writing producer/consumer indices.
    //
    MemoryBarrier();

    if ((DirectionFlags & XSK_NOTIFY_FLAG_POKE_RX) && !XskRingProducerNeedPoke(&Datapath->fillRing)) {
        DirectionFlags &= ~XSK_NOTIFY_FLAG_POKE_RX;
    }
    if ((DirectionFlags & XSK_NOTIFY_FLAG_POKE_TX) && !XskRingProducerNeedPoke(&Datapath->txRing)) {
        DirectionFlags &= ~XSK_NOTIFY_FLAG_POKE_TX;
    }

    if (DirectionFlags != 0) {
        Datapath->shared->queue->xdpApi->XskNotifySocket(
            Datapath->sock, DirectionFlags, WAIT_DRIVER_TIMEOUT_MS, &notifyResult);
    }
}

BOOLEAN
ProcessPkts(
    _Inout_ XSK_DATAPATH_WORKER *Datapath
    )
{
    XSK_NOTIFY_FLAGS notifyFlags = XSK_NOTIFY_FLAG_NONE;
    UINT32 available;
    UINT32 consumerIndex;
    UINT32 producerIndex;

    if (Datapath->flags.rx) {
        //
        // Move packets from the RX ring to the RX free ring.
        //
        available =
            RingPairReserve(
                &Datapath->rxRing, &consumerIndex, &Datapath->rxFreeRing, &producerIndex, Datapath->iobatchsize);
        if (available > 0) {
            for (UINT32 i = 0; i < available; i++) {
                XSK_BUFFER_DESCRIPTOR *rxDesc = XskRingGetElement(&Datapath->rxRing, consumerIndex++);
                UINT64 *freeDesc = XskRingGetElement(&Datapath->rxFreeRing, producerIndex++);

                *freeDesc = rxDesc->Address.BaseAddress;
            }

            XskRingConsumerRelease(&Datapath->rxRing, available);
            XskRingProducerSubmit(&Datapath->rxFreeRing, available);

            Datapath->rxPacketCount += available;
            QueryPerformanceCounter((LARGE_INTEGER*)&Datapath->rxWatchdogPerfCount);
        }

        //
        // Move packets from the RX free ring to the fill ring.
        //
        available =
            RingPairReserve(
                &Datapath->rxFreeRing, &consumerIndex, &Datapath->fillRing, &producerIndex, Datapath->iobatchsize);
        if (available > 0) {
            for (UINT32 i = 0; i < available; i++) {
                UINT64 *freeDesc = XskRingGetElement(&Datapath->rxFreeRing, consumerIndex++);
                UINT64 *fillDesc = XskRingGetElement(&Datapath->fillRing, producerIndex++);

                *fillDesc = *freeDesc;
            }

            XskRingConsumerRelease(&Datapath->rxFreeRing, available);
            XskRingProducerSubmit(&Datapath->fillRing, available);

            notifyFlags |= XSK_NOTIFY_FLAG_POKE_RX;
        }

        if (Datapath->flags.wait &&
            XskRingConsumerReserve(&Datapath->rxRing, 1, &consumerIndex) == 0 &&
            XskRingConsumerReserve(&Datapath->rxFreeRing, 1, &consumerIndex) == 0) {
            notifyFlags |= XSK_NOTIFY_FLAG_WAIT_RX;
        }

        if (Datapath->pollMode == XSK_POLL_MODE_SOCKET) {
            //
            // If socket poll mode is supported by the program, always enable pokes.
            //
            notifyFlags |= XSK_NOTIFY_FLAG_POKE_RX;
        }
    }

    if (Datapath->flags.tx) {
        //
        // Move packets from the completion ring to the TX free ring.
        //
        available =
            RingPairReserve(
                &Datapath->compRing, &consumerIndex, &Datapath->txFreeRing, &producerIndex, Datapath->iobatchsize);
        if (available > 0) {
            for (UINT32 i = 0; i < available; i++) {
                UINT64 *compDesc = XskRingGetElement(&Datapath->compRing, consumerIndex++);
                UINT64 *freeDesc = XskRingGetElement(&Datapath->txFreeRing, producerIndex++);

                *freeDesc = *compDesc;
            }

            XskRingConsumerRelease(&Datapath->compRing, available);
            XskRingProducerSubmit(&Datapath->txFreeRing, available);

            Datapath->txPacketCount += available;

            if (XskRingProducerReserve(&Datapath->txRing, MAXUINT32, &producerIndex) !=
                    Datapath->txRing.Size) {
                notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
            }

            QueryPerformanceCounter((LARGE_INTEGER*)&Datapath->txWatchdogPerfCount);
        }

        //
        // Move packets from the TX free ring to the tx ring.
        //
        available =
            RingPairReserve(
                &Datapath->txFreeRing, &consumerIndex, &Datapath->txRing, &producerIndex, Datapath->iobatchsize);
        if (available > 0) {
            for (UINT32 i = 0; i < available; i++) {
                UINT64 *freeDesc = XskRingGetElement(&Datapath->txFreeRing, consumerIndex++);
                XSK_BUFFER_DESCRIPTOR *txDesc = XskRingGetElement(&Datapath->txRing, producerIndex++);

                txDesc->Address.AddressAndOffset = *freeDesc;
                txDesc->Length = Datapath->txiosize;
            }

            XskRingConsumerRelease(&Datapath->txFreeRing, available);
            XskRingProducerSubmit(&Datapath->txRing, available);

            notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
        }

        if (Datapath->flags.wait &&
            XskRingConsumerReserve(&Datapath->compRing, 1, &consumerIndex) == 0 &&
            XskRingConsumerReserve(&Datapath->txFreeRing, 1, &consumerIndex) == 0) {
            notifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
        }

        if (Datapath->pollMode == XSK_POLL_MODE_SOCKET) {
            //
            // If socket poll mode is supported by the program, always enable pokes.
            //
            notifyFlags |= XSK_NOTIFY_FLAG_POKE_TX;
        }
    }

    if (notifyFlags != 0) {
        NotifyDriver(Datapath, notifyFlags);
    }

    //
    // TODO: Return FALSE when datapath can no longer process packets (RX/TX ring is not valid).
    //
    return TRUE;
}

VOID
PrintDatapathStats(
    _In_ CONST XSK_DATAPATH_WORKER *Datapath
    )
{
    XSK_STATISTICS stats;
    UINT32 optSize = sizeof(stats);
    CHAR rxPacketCount[64] = { 0 };
    CHAR txPacketCount[64] = { 0 };
    HRESULT res;

    res =
        Datapath->shared->queue->xdpApi->XskGetSockopt(
            Datapath->sock, XSK_SOCKOPT_STATISTICS, &stats, &optSize);
    if (FAILED(res)) {
        return;
    }
    ASSERT_FRE(optSize == sizeof(stats));

    if (Datapath->flags.rx) {
        sprintf_s(rxPacketCount, sizeof(rxPacketCount), "%llu", Datapath->rxPacketCount);
    } else {
        sprintf_s(rxPacketCount, sizeof(rxPacketCount), "n/a");
    }

    if (Datapath->flags.tx) {
        sprintf_s(txPacketCount, sizeof(txPacketCount), "%llu", Datapath->txPacketCount);
    } else {
        sprintf_s(txPacketCount, sizeof(txPacketCount), "n/a");
    }

    TraceVerbose(
        "q[%u]d[0x%p]: rx:%s tx:%s rxDrop:%llu rxTrunc:%llu "
        "rxInvalidDesc:%llu txInvalidDesc:%llu xdpMode:%s\n",
        Datapath->shared->queue->queueId, Datapath->threadHandle,
        rxPacketCount, txPacketCount, stats.RxDropped, stats.RxTruncated,
        stats.RxInvalidDescriptors, stats.TxInvalidDescriptors,
        XdpModeToString[Datapath->shared->queue->xdpMode]);
}

DWORD
WINAPI
XskDatapathWorkerFn(
    _In_ VOID *ThreadParameter
    )
{
    XSK_DATAPATH_WORKER *datapath = ThreadParameter;
    QUEUE_CONTEXT *queue = datapath->shared->queue;

    TraceEnter("q[%u]d[0x%p]", queue->queueId, datapath->threadHandle);

    if (SUCCEEDED(InitializeDatapath(datapath))) {
        while (!ReadBooleanNoFence(&done)) {
            if (ReadNoFence((LONG *)&datapath->shared->state) == ThreadStateReturn) {
                break;
            }

            if (!ProcessPkts(datapath)) {
                break;
            }
        }

        if (extraStats) {
            PrintDatapathStats(datapath);
        }
        CleanupDatapath(datapath);
    }

    TraceExit("q[%u]d[0x%p]", queue->queueId, datapath->threadHandle);
    return 0;
}

DWORD
WINAPI
XskFuzzerWorkerFn(
    _In_ VOID *ThreadParameter
    )
{
    XSK_FUZZER_WORKER *fuzzer = ThreadParameter;
    QUEUE_CONTEXT *queue = fuzzer->shared->queue;
    SCENARIO_CONFIG *scenarioConfig = &queue->scenarioConfig;

    TraceEnter("q[%u]f[0x%p]", queue->queueId, fuzzer->threadHandle);

    while (!ReadBooleanNoFence(&done)) {
        if (ReadNoFence((LONG *)&fuzzer->shared->state) == ThreadStateReturn) {
            break;
        }

        if (ReadNoFence((LONG *)&fuzzer->shared->state) == ThreadStatePause) {
            WaitForSingleObject(fuzzer->shared->pauseEvent, INFINITE);
            continue;
        }

        FuzzSocketUmemSetup(queue, queue->sock, &scenarioConfig->isUmemRegistered);
        if (queue->sharedUmemSock != NULL) {
            FuzzSocketSharedUmemSetup(
                queue, queue->sharedUmemSock, queue->sock);
        }

        FuzzInterface(fuzzer, queue);

        FuzzSocketRxTxSetup(
            queue, queue->sock,
            scenarioConfig->sockRx, scenarioConfig->sockTx,
            &scenarioConfig->isSockRxSet, &scenarioConfig->isSockTxSet);
        FuzzSocketMisc(queue, queue->sock, &queue->rxProgramSet);

        if (queue->sharedUmemSock != NULL) {
            FuzzSocketRxTxSetup(
                queue, queue->sharedUmemSock,
                scenarioConfig->sharedUmemSockRx, scenarioConfig->sharedUmemSockTx,
                &scenarioConfig->isSharedUmemSockRxSet, &scenarioConfig->isSharedUmemSockTxSet);
            FuzzSocketMisc(queue, queue->sharedUmemSock, &queue->sharedUmemRxProgramSet);
        }

        FuzzSocketBind(
            queue, queue->sock, scenarioConfig->sockRx, scenarioConfig->sockTx,
            &scenarioConfig->isSockBound);
        if (queue->sharedUmemSock != NULL) {
            FuzzSocketBind(
                queue, queue->sharedUmemSock, scenarioConfig->sharedUmemSockRx,
                scenarioConfig->sharedUmemSockTx, &scenarioConfig->isSharedUmemSockBound);
        }

        FuzzSocketActivate(queue, queue->sock, &scenarioConfig->isSockActivated);
        if (queue->sharedUmemSock != NULL) {
            FuzzSocketActivate(
                queue, queue->sharedUmemSock, &scenarioConfig->isSharedUmemSockActivated);
        }

        if (ScenarioConfigComplete(scenarioConfig)) {
            if (WaitForSingleObject(scenarioConfig->completeEvent, 0) != WAIT_OBJECT_0) {
                TraceVerbose("q[%u]f[0x%p]: marking socket setup complete",
                    queue->queueId, fuzzer->threadHandle);
                SetEvent(scenarioConfig->completeEvent);
            }
            WaitForSingleObject(stopEvent, 50);
        }
    }

    if (fuzzer->fuzzerInterface != NULL) {
        ASSERT_FRE(CloseHandle(fuzzer->fuzzerInterface));
    }

    TraceExit("q[%u]f[0x%p]", queue->queueId, fuzzer->threadHandle);
    return 0;
}

VOID
UpdateSetupStats(
    _In_ QUEUE_WORKER *QueueWorker,
    _In_ QUEUE_CONTEXT *Queue
    )
{
    //
    // Every scenario requires at least one UMEM registered and one socket bound.
    //
    ++QueueWorker->setupStats.initSuccess;
    ++QueueWorker->setupStats.umemTotal;
    ++QueueWorker->setupStats.bindTotal;
    ++QueueWorker->setupStats.activateTotal;

    if (Queue->scenarioConfig.isUmemRegistered) {
        ++QueueWorker->setupStats.umemSuccess;
    }
    if (Queue->scenarioConfig.sockRx) {
        ++QueueWorker->setupStats.rxTotal;
        if (Queue->scenarioConfig.isSockRxSet) {
            ++QueueWorker->setupStats.rxSuccess;
        }
    }
    if (Queue->scenarioConfig.sockTx) {
        ++QueueWorker->setupStats.txTotal;
        if (Queue->scenarioConfig.isSockTxSet) {
            ++QueueWorker->setupStats.txSuccess;
        }
    }
    if (Queue->scenarioConfig.isSockBound) {
        ++QueueWorker->setupStats.bindSuccess;
    }
    if (Queue->scenarioConfig.isSockActivated) {
        ++QueueWorker->setupStats.activateSuccess;
    }
    if (Queue->scenarioConfig.sharedUmemSockRx) {
        ++QueueWorker->setupStats.sharedRxTotal;
        if (Queue->scenarioConfig.isSharedUmemSockRxSet) {
            ++QueueWorker->setupStats.sharedRxSuccess;
        }
    }
    if (Queue->scenarioConfig.sharedUmemSockTx) {
        ++QueueWorker->setupStats.sharedTxTotal;
        if (Queue->scenarioConfig.isSharedUmemSockTxSet) {
            ++QueueWorker->setupStats.sharedTxSuccess;
        }
    }
    if (Queue->scenarioConfig.sharedUmemSockRx || Queue->scenarioConfig.sharedUmemSockTx) {
        ++QueueWorker->setupStats.sharedBindTotal;
        ++QueueWorker->setupStats.sharedActivateTotal;
        if (Queue->scenarioConfig.isSharedUmemSockBound) {
            ++QueueWorker->setupStats.sharedBindSuccess;
        }
        if (Queue->scenarioConfig.isSharedUmemSockActivated) {
            ++QueueWorker->setupStats.sharedActivateSuccess;
        }
    }
    if (ScenarioConfigComplete(&Queue->scenarioConfig)) {
        ++QueueWorker->setupStats.setupSuccess;
    }
}

VOID
PrintSetupStats(
    _In_ QUEUE_WORKER *QueueWorker,
    _In_ ULONG NumIterations
    )
{
    SETUP_STATS *setupStats = &QueueWorker->setupStats;

    printf(
        "\tbreakdown\n"
        "\tinit:           (%lu / %lu) %lu%%\n"
        "\tumem:           (%lu / %lu) %lu%%\n"
        "\trx:             (%lu / %lu) %lu%%\n"
        "\ttx:             (%lu / %lu) %lu%%\n"
        "\tbind:           (%lu / %lu) %lu%%\n"
        "\tactivate:       (%lu / %lu) %lu%%\n"
        "\tsharedRx:       (%lu / %lu) %lu%%\n"
        "\tsharedTx:       (%lu / %lu) %lu%%\n"
        "\tsharedBind:     (%lu / %lu) %lu%%\n"
        "\tsharedActivate: (%lu / %lu) %lu%%\n",
        setupStats->initSuccess, NumIterations, Pct(setupStats->initSuccess, NumIterations),
        setupStats->umemSuccess, setupStats->umemTotal, Pct(setupStats->umemSuccess, setupStats->umemTotal),
        setupStats->rxSuccess, setupStats->rxTotal, Pct(setupStats->rxSuccess, setupStats->rxTotal),
        setupStats->txSuccess, setupStats->txTotal, Pct(setupStats->txSuccess, setupStats->txTotal),
        setupStats->bindSuccess, setupStats->bindTotal, Pct(setupStats->bindSuccess, setupStats->bindTotal),
        setupStats->activateSuccess, setupStats->activateTotal, Pct(setupStats->activateSuccess, setupStats->activateTotal),
        setupStats->sharedRxSuccess, setupStats->sharedRxTotal, Pct(setupStats->sharedRxSuccess, setupStats->sharedRxTotal),
        setupStats->sharedTxSuccess, setupStats->sharedTxTotal, Pct(setupStats->sharedTxSuccess, setupStats->sharedTxTotal),
        setupStats->sharedBindSuccess, setupStats->sharedBindTotal, Pct(setupStats->sharedBindSuccess, setupStats->sharedBindTotal),
        setupStats->sharedActivateSuccess, setupStats->sharedActivateTotal, Pct(setupStats->sharedActivateSuccess, setupStats->sharedActivateTotal));
}

DWORD
WINAPI
QueueWorkerFn(
    _In_ VOID *ThreadParameter
    )
{
    QUEUE_WORKER *queueWorker = ThreadParameter;
    ULONG numIterations = 0;
    ULONG numSuccessfulSetups = 0;
    ULONG successPct;

    TraceEnter("q[%u]", queueWorker->queueId);

    while (!ReadBooleanNoFence(&done)) {
        QUEUE_CONTEXT *queue;
        DWORD ret;

        ++numIterations;
        TraceVerbose("q[%u]: iter %lu", queueWorker->queueId, numIterations);

        QueryPerformanceCounter((LARGE_INTEGER*)&queueWorker->watchdogPerfCount);

        if (!SUCCEEDED(InitializeQueue(queueWorker->queueId, &queue))) {
            TraceVerbose("q[%u]: failed to setup queue", queueWorker->queueId);
            continue;
        }

        //
        // Hand off socket/s to fuzzer threads.
        //
        for (UINT32 i = 0; i < queue->fuzzerCount; i++) {
            queue->fuzzers[i].threadHandle =
                CreateThread(NULL, 0, XskFuzzerWorkerFn, &queue->fuzzers[i], 0, NULL);
        }

        //
        // Wait until fuzzers have successfully configured the socket/s.
        //
        TraceVerbose("q[%u]: waiting for sockets to be configured", queue->queueId);
        ret = WaitForSingleObject(queue->scenarioConfig.completeEvent, 500);

        if (ret == WAIT_OBJECT_0) {
            ++numSuccessfulSetups;

            //
            // Hand off configured socket/s to datapath thread/s.
            //
            if (queue->datapath1.sock != NULL) {
                queue->datapath1.threadHandle =
                    CreateThread(NULL, 0, XskDatapathWorkerFn, &queue->datapath1, 0, NULL);
            }
            if (queue->datapath2.sock != NULL) {
                queue->datapath2.threadHandle =
                    CreateThread(NULL, 0, XskDatapathWorkerFn, &queue->datapath2, 0, NULL);
            }

            //
            // Let datapath thread/s pump datapath for set duration.
            //
            TraceVerbose("q[%u]: letting datapath pump", queue->queueId);
            WaitForSingleObject(stopEvent, 500);

            //
            // Signal and wait for datapath thread/s to return.
            //
            TraceVerbose("q[%u]: waiting for datapath threads", queue->queueId);
            WriteNoFence((LONG *)&queue->datapathShared.state, ThreadStateReturn);
            if (queue->datapath1.threadHandle != NULL) {
                #pragma warning(push)
                #pragma warning(disable:6387) // threadHandle is not NULL.
                WaitForSingleObject(queue->datapath1.threadHandle, INFINITE);
                ASSERT_FRE(CloseHandle(queue->datapath1.threadHandle));
                queue->datapath1.threadHandle = NULL;
                #pragma warning(pop)
            }
            if (queue->datapath2.threadHandle != NULL) {
                #pragma warning(push)
                #pragma warning(disable:6387) // threadHandle is not NULL.
                WaitForSingleObject(queue->datapath2.threadHandle, INFINITE);
                ASSERT_FRE(CloseHandle(queue->datapath2.threadHandle));
                queue->datapath2.threadHandle = NULL;
                #pragma warning(pop)
            }
        }

        //
        // Signal and wait for fuzzer threads to return.
        //
        TraceVerbose("q[%u]: waiting for fuzzer threads", queue->queueId);
        WriteNoFence((LONG*)&queue->fuzzerShared.state, ThreadStateReturn);
        for (UINT32 i = 0; i < queue->fuzzerCount; i++) {
            #pragma warning(push)
            #pragma warning(disable:6387) // threadHandle is not NULL.
            WaitForSingleObject(queue->fuzzers[i].threadHandle, INFINITE);
            ASSERT_FRE(CloseHandle(queue->fuzzers[i].threadHandle));
            queue->fuzzers[i].threadHandle = NULL;
            #pragma warning(pop)
        }

        if (extraStats) {
            UpdateSetupStats(queueWorker, queue);
        }

        CleanupQueue(queue);
    }

    successPct = Pct(numSuccessfulSetups, numIterations);
    printf("q[%u]: socket setup success rate: (%lu / %lu) %lu%%\n",
        queueWorker->queueId, numSuccessfulSetups, numIterations, successPct);

    if (extraStats) {
        PrintSetupStats(queueWorker, numIterations);
    }

    //
    // Require a certain percentage (1%) of sockets to complete the setup phase
    // as a proxy for ensuring effective code coverage, validating that all
    // drivers are started, etc.
    //
    ASSERT_FRE(successPct >= successThresholdPercent);

    TraceExit("q[%u]", queueWorker->queueId);
    return 0;
}

DWORD
WINAPI
AdminFn(
    _In_ VOID *ThreadParameter
    )
{
    DWORD res;
    CHAR cmdBuff[256];
    HKEY xdpParametersKey;
    CONST CHAR *delayDetachTimeoutRegName = "GenericDelayDetachTimeoutSec";

    UNREFERENCED_PARAMETER(ThreadParameter);

    TraceEnter("-");

    res =
        RegCreateKeyExA(
            HKEY_LOCAL_MACHINE,
            "System\\CurrentControlSet\\Services\\Xdp\\Parameters",
            0, NULL, REG_OPTION_VOLATILE, KEY_WRITE, NULL, &xdpParametersKey, NULL);
    ASSERT_FRE(res == ERROR_SUCCESS);

    while (TRUE) {
        res = WaitForSingleObject(workersDoneEvent, ADMIN_THREAD_TIMEOUT_SEC * 1000);
        if (res == WAIT_OBJECT_0) {
            break;
        }

        TraceVerbose("admin iter");

        if (!cleanDatapath && !(RandUlong() % 10)) {
            INT exitCode;
            TraceVerbose("admin: restart adapter");
            RtlZeroMemory(cmdBuff, sizeof(cmdBuff));
            sprintf_s(
                cmdBuff, sizeof(cmdBuff),
                "%s -Command \"(Get-NetAdapter | Where-Object {$_.IfIndex -eq %d}) | Restart-NetAdapter\"",
                powershellPrefix, ifindex);
            exitCode = system(cmdBuff);
            TraceVerbose("admin: restart adapter exitCode=%d", exitCode);
        }

        if (!(RandUlong() % 10)) {
            DWORD delayDetachTimeout = RandUlong() % 10;
            LSTATUS regStatus;
            TraceVerbose("admin: set delayDetachTimeout=%u", delayDetachTimeout);
            regStatus = RegSetValueExA(
                xdpParametersKey, delayDetachTimeoutRegName, 0, REG_DWORD,
                (BYTE *)&delayDetachTimeout, sizeof(delayDetachTimeout));
            TraceVerbose("admin: set delayDetachTimeout regStatus=%d", regStatus);
        }

        if (!(RandUlong() % 10)) {
            INT exitCode;
            TraceVerbose("admin: query counters");
            RtlZeroMemory(cmdBuff, sizeof(cmdBuff));
            sprintf_s(
                cmdBuff, sizeof(cmdBuff),
                "%s -Command \"Get-Counter -Counter (Get-Counter -ListSet XDP*).Paths -ErrorAction Ignore | Out-Null\"",
                powershellPrefix);
            exitCode = system(cmdBuff);
            TraceVerbose("admin: query counters exitCode=%d", exitCode);
        }
    }

    //
    // Clean up fuzzed registry state.
    //
    RegDeleteValueA(xdpParametersKey, delayDetachTimeoutRegName);
    RegCloseKey(xdpParametersKey);

    TraceExit("-");

    return 0;
}

DWORD
WINAPI
WatchdogFn(
    _In_ VOID *ThreadParameter
    )
{
    DWORD res;
    ULONGLONG perfCount;
    ULONGLONG watchdogTimeoutInCounts = perfFreq * WATCHDOG_THREAD_TIMEOUT_SEC;

    UNREFERENCED_PARAMETER(ThreadParameter);

    TraceEnter("-");

    while (TRUE) {
        res = WaitForSingleObject(workersDoneEvent, WATCHDOG_THREAD_TIMEOUT_SEC * 1000);
        if (res == WAIT_OBJECT_0) {
            break;
        }

        TraceVerbose("watchdog iter");

        QueryPerformanceCounter((LARGE_INTEGER*)&perfCount);
        for (UINT32 i = 0; i < queueCount; i++) {
            if ((LONGLONG)(queueWorkers[i].watchdogPerfCount + watchdogTimeoutInCounts - perfCount) < 0) {
                TraceError( "WATCHDOG exceeded on queue %d", i);
                printf("WATCHDOG exceeded on queue %d\n", i);
                if (strlen(watchdogCmd) > 0) {
                    if (!_stricmp(watchdogCmd, "break")) {
                        DbgRaiseAssertionFailure();
                    } else {
                        TraceInfo("watchdogCmd=%s", watchdogCmd);
                        system(watchdogCmd);
                    }
                }
                exit(ERROR_TIMEOUT);
            }
        }
    }

    TraceExit("-");
    return 0;
}

VOID
PrintUsage(
    _In_ INT Line
    )
{
    printf("Line:%d\n", Line);
    printf(HELP);
    exit(1);
}

VOID
ParseArgs(
    INT argc,
    CHAR **argv
    )
{
    INT i = 1;

    if (argc < 3) {
        Usage();
    }

    if (strcmp(argv[i++], "-IfIndex")) {
        Usage();
    }
    ifindex = atoi(argv[i++]);

    while (i < argc) {
        if (!strcmp(argv[i], "-Minutes")) {
            if (++i >= argc) {
                Usage();
            }
            duration = atoi(argv[i]) * 60;
            TraceVerbose("duration=%u", duration);
        } else if (!strcmp(argv[i], "-Stats")) {
            extraStats = TRUE;
        } else if (!strcmp(argv[i], "-Verbose")) {
            verbose = TRUE;
        } else if (!strcmp(argv[i], "-QueueCount")) {
            if (++i >= argc) {
                Usage();
            }
            queueCount = atoi(argv[i]);
            TraceVerbose("queueCount=%u", queueCount);
        } else if (!strcmp(argv[i], "-FuzzerCount")) {
            if (++i >= argc) {
                Usage();
            }
            fuzzerCount = atoi(argv[i]);
            TraceVerbose("fuzzerCount=%u", fuzzerCount);
        } else if (!strcmp(argv[i], "-CleanDatapath")) {
            cleanDatapath = TRUE;
            TraceVerbose("cleanDatapath=%!BOOLEAN!", cleanDatapath);
        } else if (!strcmp(argv[i], "-WatchdogCmd")) {
            if (++i >= argc) {
                Usage();
            }
            watchdogCmd = argv[i];
            TraceVerbose("watchdogCmd=%s", watchdogCmd);
        } else if (!strcmp(argv[i], "-SuccessThresholdPercent")) {
            if (++i >= argc) {
                Usage();
            }
            successThresholdPercent = (UINT8)atoi(argv[i]);
            TraceVerbose("successThresholdPercent=%u", successThresholdPercent);
        } else if (!strcmp(argv[i], "-EnableEbpf")) {
            enableEbpf = TRUE;
        } else {
            Usage();
        }

        ++i;
    }

    if (ifindex == -1) {
        Usage();
    }
}

BOOL
WINAPI
ConsoleCtrlHandler(
    _In_ DWORD CtrlType
    )
{
    UNREFERENCED_PARAMETER(CtrlType);

    TraceVerbose("CTRL-C");

    //
    // Gracefully initiate a stop.
    //
    SetEvent(stopEvent);

    return TRUE;
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    HANDLE adminThread;
    HANDLE watchdogThread;

    WPP_INIT_TRACING(NULL);

#if DBG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#endif

    ParseArgs(argc, argv);

    powershellPrefix = GetPowershellPrefix();

    QueryPerformanceFrequency((LARGE_INTEGER*)&perfFreq);

    stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ASSERT_FRE(stopEvent != NULL);

    workersDoneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ASSERT_FRE(workersDoneEvent != NULL);

    ASSERT_FRE(SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE));

    //
    // Allocate and initialize queue workers.
    //

    queueWorkers = calloc(queueCount, sizeof(*queueWorkers));
    ASSERT_FRE(queueWorkers != NULL);

    for (UINT32 i = 0; i < queueCount; i++) {
        QUEUE_WORKER *queueWorker = &queueWorkers[i];
        queueWorker->queueId = i;
        QueryPerformanceCounter((LARGE_INTEGER*)&queueWorker->watchdogPerfCount);
    }

    //
    // Create admin and watchdog thread for queue workers.
    //
    adminThread = CreateThread(NULL, 0, AdminFn, NULL, 0, NULL);
    ASSERT_FRE(adminThread);
    watchdogThread = CreateThread(NULL, 0, WatchdogFn, NULL, 0, NULL);
    ASSERT_FRE(watchdogThread);

    //
    // Kick off the queue workers.
    //
    for (UINT32 i = 0; i < queueCount; i++) {
        QUEUE_WORKER *queueWorker = &queueWorkers[i];
        queueWorker->threadHandle =
            CreateThread(NULL, 0, QueueWorkerFn, queueWorker, 0, NULL);
        ASSERT_FRE(queueWorker->threadHandle != NULL);
    }

    //
    // Wait for test duration.
    //
    TraceVerbose("main: running test...");
    WaitForSingleObject(stopEvent, (duration == ULONG_MAX) ? INFINITE : duration * 1000);
    WriteBooleanNoFence(&done, TRUE);

    //
    // Wait on each queue worker to return.
    //
    TraceVerbose("main: waiting for workers...");
    for (UINT32 i = 0; i < queueCount; i++) {
        QUEUE_WORKER *queueWorker = &queueWorkers[i];
        #pragma warning(push)
        #pragma warning(disable:6387) // threadHandle is not NULL.
        WaitForSingleObject(queueWorker->threadHandle, INFINITE);
        ASSERT_FRE(CloseHandle(queueWorker->threadHandle));
        queueWorker->threadHandle = NULL;
        #pragma warning(pop)
    }

    //
    // Cleanup the admin and watchdog threads after all workers have exited.
    //

    SetEvent(workersDoneEvent);

    TraceVerbose("main: waiting for admin...");
    WaitForSingleObject(adminThread, INFINITE);
    ASSERT_FRE(CloseHandle(adminThread));
    adminThread = NULL;

    TraceVerbose("main: waiting for watchdog...");
    WaitForSingleObject(watchdogThread, INFINITE);
    ASSERT_FRE(CloseHandle(watchdogThread));
    watchdogThread = NULL;

    free(queueWorkers);

    printf("done\n");

    WPP_CLEANUP();

    return 0;
}
