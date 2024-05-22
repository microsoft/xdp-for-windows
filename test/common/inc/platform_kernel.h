//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#if !defined(_KERNEL_MODE)
#error "Incorrectly including Kernel Platform Header"
#endif

#include "xdpapi_status.h"

#ifndef KRTL_INIT_SEGMENT
#define KRTL_INIT_SEGMENT "INIT"
#endif
#ifndef KRTL_PAGE_SEGMENT
#define KRTL_PAGE_SEGMENT "PAGE"
#endif
#ifndef KRTL_NONPAGED_SEGMENT
#define KRTL_NONPAGED_SEGMENT ".text"
#endif

// Use on code in the INIT segment. (Code is discarded after DriverEntry returns.)
#define INITCODE __declspec(code_seg(KRTL_INIT_SEGMENT))

// Use on pageable functions.
#define PAGEDX __declspec(code_seg(KRTL_PAGE_SEGMENT))

typedef INT8 int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;

typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;

#define UINT8_MAX   0xffui8
#define UINT16_MAX  0xffffui16
#define UINT32_MAX  0xffffffffui32
#define UINT64_MAX  0xffffffffffffffffui64

//
// Memory
//

#define CXPLAT_ALLOC_NONPAGED(Size, Tag) \
    ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, Size, Tag)
#define CXPLAT_FREE(Mem, Tag) ExFreePoolWithTag((void*)Mem, Tag)
#define CXPLAT_VIRTUAL_ALLOC(Size, TypeFlags, ProtectionFlags, Tag) \
    CXPLAT_ALLOC_NONPAGED(Size, Tag)
#define CXPLAT_VIRTUAL_FREE(Mem, Size, Type, Tag) CXPLAT_FREE(Mem, Tag);

//
// Event
//

typedef KEVENT CXPLAT_EVENT;
#define CxPlatEventInitialize(Event, ManualReset, InitialState) \
    KeInitializeEvent(Event, ManualReset ? NotificationEvent : SynchronizationEvent, InitialState)
#define CxPlatEventUninitialize(Event) UNREFERENCED_PARAMETER(Event)
#define CxPlatEventSet(Event) KeSetEvent(&(Event), IO_NO_INCREMENT, FALSE)
#define CxPlatEventReset(Event) KeResetEvent(&(Event))
#define CxPlatEventWaitForever(Event) \
    KeWaitForSingleObject(&(Event), Executive, KernelMode, FALSE, NULL)
inline
NTSTATUS
_CxPlatEventWaitWithTimeout(
    _In_ CXPLAT_EVENT* Event,
    _In_ UINT32 TimeoutMs
    )
{
    LARGE_INTEGER Timeout100Ns;
    Timeout100Ns.QuadPart = -1 * Int32x32To64(TimeoutMs, 10000);
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout100Ns);
}
#define CxPlatEventWaitWithTimeout(Event, TimeoutMs) \
    (STATUS_SUCCESS == _CxPlatEventWaitWithTimeout(&Event, TimeoutMs))

//
// Processor Count and Index
//

inline
BOOLEAN
CxPlatSetThreadNodeAffinity(
    LONG NodeAffinity
    )
{
    GROUP_AFFINITY group;

    KeQueryNodeActiveAffinity((USHORT)NodeAffinity, &group, NULL);
    KeSetSystemGroupAffinityThread(&group, NULL);

    return TRUE;
}

inline
BOOLEAN
CxPlatSetThreadGroupAffinity(
    LONG GroupNumber,
    DWORD_PTR CpuAffinity
    )
{
    GROUP_AFFINITY group = {0};

    group.Mask = CpuAffinity;
    group.Group = (USHORT)GroupNumber;

    KeSetSystemGroupAffinityThread(&group, NULL);

    return TRUE;
}

//
// Time Measurement
//

//
// Returns the worst-case system timer resolution (in us).
//
inline
uint64_t
CxPlatGetTimerResolution()
{
    ULONG MaximumTime, MinimumTime, CurrentTime;
    ExQueryTimerResolution(&MaximumTime, &MinimumTime, &CurrentTime);
    return NS100_TO_US(MaximumTime);
}

//
// Performance counter frequency.
//
extern uint64_t CxPlatPerfFreq;

//
// Returns the current time in platform specific time units.
//
inline
uint64_t
QuicTimePlat(
    void
    )
{
    return (uint64_t)KeQueryPerformanceCounter(NULL).QuadPart;
}

//
// Converts platform time to microseconds.
//
inline
uint64_t
QuicTimePlatToUs64(
    uint64_t Count
    )
{
    //
    // Multiply by a big number (1000000, to convert seconds to microseconds)
    // and divide by a big number (CxPlatPerfFreq, to convert counts to secs).
    //
    // Avoid overflow with separate multiplication/division of the high and low
    // bits. Taken from TcpConvertPerformanceCounterToMicroseconds.
    //
    uint64_t High = (Count >> 32) * 1000000;
    uint64_t Low = (Count & 0xFFFFFFFF) * 1000000;
    return
        ((High / CxPlatPerfFreq) << 32) +
        ((Low + ((High % CxPlatPerfFreq) << 32)) / CxPlatPerfFreq);
}

//
// Converts microseconds to platform time.
//
inline
uint64_t
CxPlatTimeUs64ToPlat(
    uint64_t TimeUs
    )
{
    uint64_t High = (TimeUs >> 32) * CxPlatPerfFreq;
    uint64_t Low = (TimeUs & 0xFFFFFFFF) * CxPlatPerfFreq;
    return
        ((High / 1000000) << 32) +
        ((Low + ((High % 1000000) << 32)) / 1000000);
}

#define CxPlatTimeUs64() QuicTimePlatToUs64(QuicTimePlat())
#define CxPlatTimeUs32() (uint32_t)CxPlatTimeUs64()
#define CxPlatTimeMs64() US_TO_MS(CxPlatTimeUs64())
#define CxPlatTimeMs32() (uint32_t)CxPlatTimeMs64()

#define UNIX_EPOCH_AS_FILE_TIME 0x19db1ded53e8000ll

inline
int64_t
CxPlatTimeEpochMs64(
    )
{
    LARGE_INTEGER SystemTime;
    KeQuerySystemTime(&SystemTime);
    return NS100_TO_MS(SystemTime.QuadPart - UNIX_EPOCH_AS_FILE_TIME);
}

//
// Thread
//

typedef struct CXPLAT_THREAD_CONFIG {
    UINT16 Flags;
    UINT16 IdealProcessor;
    _Field_z_ const char* Name;
    KSTART_ROUTINE* Callback;
    void* Context;
} CXPLAT_THREAD_CONFIG;

typedef struct _ETHREAD *CXPLAT_THREAD;
#define CXPLAT_THREAD_CALLBACK(FuncName, CtxVarName)  \
    _Function_class_(KSTART_ROUTINE)                \
    _IRQL_requires_same_                            \
    void                                            \
    FuncName(                                       \
      _In_ void* CtxVarName                         \
      )

#define CXPLAT_THREAD_RETURN(Status) PsTerminateSystemThread(Status)

inline
XDP_STATUS
CxPlatThreadCreate(
    _In_ CXPLAT_THREAD_CONFIG* Config,
    _Out_ CXPLAT_THREAD* Thread
    )
{
    XDP_STATUS Status;
    HANDLE ThreadHandle;
    Status =
        PsCreateSystemThread(
            &ThreadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            NULL,
            NULL,
            Config->Callback,
            Config->Context);
    CXPLAT_DBG_ASSERT(XDP_SUCCEEDED(Status));
    if (XDP_FAILED(Status)) {
        *Thread = NULL;
        goto Error;
    }
    Status =
        ObReferenceObjectByHandle(
            ThreadHandle,
            THREAD_ALL_ACCESS,
            *PsThreadType,
            KernelMode,
            (void**)Thread,
            NULL);
    CXPLAT_DBG_ASSERT(XDP_SUCCEEDED(Status));
    if (XDP_FAILED(Status)) {
        *Thread = NULL;
        goto Cleanup;
    }
    PROCESSOR_NUMBER Processor, IdealProcessor;
    Status =
        KeGetProcessorNumberFromIndex(
            Config->IdealProcessor,
            &Processor);
    if (XDP_FAILED(Status)) {
        Status = XDP_STATUS_SUCCESS; // Currently we don't treat this as fatal
        goto SetPriority;             // TODO: Improve this logic.
    }
    IdealProcessor = Processor;
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_AFFINITIZE) {
        GROUP_AFFINITY Affinity;
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Group = Processor.Group;
        Affinity.Mask = (1ull << Processor.Number);
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadGroupInformation,
                &Affinity,
                sizeof(Affinity));
        CXPLAT_DBG_ASSERT(XDP_SUCCEEDED(Status));
        if (XDP_FAILED(Status)) {
            goto Cleanup;
        }
    } else { // NUMA Node Affinity
        // SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info;
        // ULONG InfoLength = sizeof(Info);
        // Status =
        //     KeQueryLogicalProcessorRelationship(
        //         &Processor,
        //         RelationNumaNode,
        //         &Info,
        //         &InfoLength);
        // CXPLAT_DBG_ASSERT(XDP_SUCCEEDED(Status));
        // if (XDP_FAILED(Status)) {
        //     goto Cleanup;
        // }
        // Status =
        //     ZwSetInformationThread(
        //         ThreadHandle,
        //         ThreadGroupInformation,
        //         &Info.NumaNode.GroupMask,
        //         sizeof(GROUP_AFFINITY));
        // CXPLAT_DBG_ASSERT(XDP_SUCCEEDED(Status));
        // if (XDP_FAILED(Status)) {
        //     goto Cleanup;
        // }
    }
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_IDEAL_PROC) {
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadIdealProcessorEx,
                &IdealProcessor, // Don't pass in Processor because this overwrites on output.
                sizeof(IdealProcessor));
        CXPLAT_DBG_ASSERT(XDP_SUCCEEDED(Status));
        if (XDP_FAILED(Status)) {
            goto Cleanup;
        }
    }
SetPriority:
    if (Config->Flags & CXPLAT_THREAD_FLAG_HIGH_PRIORITY) {
        KeSetBasePriorityThread(
            (PKTHREAD)(*Thread),
            IO_NETWORK_INCREMENT + 1);
    }
Cleanup:
    ZwClose(ThreadHandle);
Error:
    return Status;
}
#define CxPlatThreadDelete(Thread) ObDereferenceObject(*(Thread))
#define CxPlatThreadWait(Thread) \
    KeWaitForSingleObject( \
        *(Thread), \
        Executive, \
        KernelMode, \
        FALSE, \
        NULL)

//
// Initializes the PAL library. Calls to this and
// CxPlatformUninitialize must be serialized and cannot overlap.
//
PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatInitialize(
    void
    );

//
// Uninitializes the PAL library. Calls to this and
// CxPlatformInitialize must be serialized and cannot overlap.
//
PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    );
