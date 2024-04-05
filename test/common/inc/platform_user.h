//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifdef _KERNEL_MODE
#error "Incorrectly including User Platform Header"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#include <windows.h>
#include <stdlib.h>
#include <stdint.h>

EXTERN_C_START

//
// Wrapper functions
//

//
// CloseHandle has an incorrect SAL annotation, so call through a wrapper.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
inline
void
CxPlatCloseHandle(_Pre_notnull_ HANDLE Handle) {
    CloseHandle(Handle);
}

//
// Memory
//

#define CXPLAT_ALLOC_NONPAGED(Size, Tag) malloc(Size)
#define CXPLAT_FREE(Mem, Tag) free((void*)Mem)
#define CXPLAT_VIRTUAL_ALLOC(Size, TypeFlags, ProtectionFlags, Tag) \
    VirtualAlloc(NULL, Size, TypeFlags, ProtectionFlags)
#define CXPLAT_VIRTUAL_FREE(Mem, Size, Type, Tag) VirtualFree(Mem, Size, Type);

//
// Event
//

typedef HANDLE CXPLAT_EVENT;
#define CxPlatEventInitialize(Event, ManualReset, InitialState) \
    *(Event) = CreateEvent(NULL, ManualReset, InitialState, NULL)
#define CxPlatEventUninitialize(Event) CxPlatCloseHandle(Event)
#define CxPlatEventSet(Event) SetEvent(Event)
#define CxPlatEventReset(Event) ResetEvent(Event)
#define CxPlatEventWaitForever(Event) WaitForSingleObject(Event, INFINITE)
#define CxPlatEventWaitWithTimeout(Event, timeoutMs) \
    (WAIT_OBJECT_0 == WaitForSingleObject(Event, timeoutMs))

//
// Processor Count and Index
//

typedef struct CXPLAT_PROCESSOR_INFO {
    uint32_t Index;  // Index in the current group
    uint16_t Group;  // The group number this processor is a part of
} CXPLAT_PROCESSOR_INFO;

typedef struct CXPLAT_PROCESSOR_GROUP_INFO {
    KAFFINITY Mask;  // Bit mask of active processors in the group
    uint32_t Count;  // Count of active processors in the group
    uint32_t Offset; // Base process index offset this group starts at
} CXPLAT_PROCESSOR_GROUP_INFO;

extern CXPLAT_PROCESSOR_INFO* CxPlatProcessorInfo;
extern CXPLAT_PROCESSOR_GROUP_INFO* CxPlatProcessorGroupInfo;

extern uint32_t CxPlatProcessorCount;
#define CxPlatProcCount() CxPlatProcessorCount

_IRQL_requires_max_(DISPATCH_LEVEL)
inline
uint32_t
CxPlatProcCurrentNumber(
    void
    ) {
    PROCESSOR_NUMBER ProcNumber;
    GetCurrentProcessorNumberEx(&ProcNumber);
    const CXPLAT_PROCESSOR_GROUP_INFO* Group = &CxPlatProcessorGroupInfo[ProcNumber.Group];
    return Group->Offset + (ProcNumber.Number % Group->Count);
}

inline
BOOLEAN
CxPlatSetThreadNodeAffinity(
    LONG NodeAffinity
    )
{
    GROUP_AFFINITY group;

    if (!GetNumaNodeProcessorMaskEx((USHORT)NodeAffinity, &group)) {
        CXPLAT_DBG_ASSERT(FALSE);
        return FALSE;
    }
    if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
        CXPLAT_DBG_ASSERT(FALSE);
        return FALSE;
    }

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
    group.Group = (WORD)GroupNumber;
    if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
        CXPLAT_DBG_ASSERT(FALSE);
        return FALSE;
    }

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
    DWORD Adjustment, Increment;
    BOOL AdjustmentDisabled;
    GetSystemTimeAdjustment(&Adjustment, &Increment, &AdjustmentDisabled);
    return NS100_TO_US(Increment);
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
CxPlatTimePlat(
    void
    )
{
    uint64_t Count;
    QueryPerformanceCounter((LARGE_INTEGER*)&Count);
    return Count;
}

//
// Converts platform time to microseconds.
//
inline
uint64_t
CxPlatTimePlatToUs64(
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
        ((Low + ((High % 1000000) << 32)) / CxPlatPerfFreq);
}

#define CxPlatTimeUs64() CxPlatTimePlatToUs64(CxPlatTimePlat())
#define CxPlatTimeUs32() (uint32_t)CxPlatTimeUs64()
#define CxPlatTimeMs64() US_TO_MS(CxPlatTimeUs64())
#define CxPlatTimeMs32() (uint32_t)CxPlatTimeMs64()

#define UNIX_EPOCH_AS_FILE_TIME 0x19db1ded53e8000ll

inline
int64_t
CxPlatTimeEpochMs64(
    )
{
    LARGE_INTEGER FileTime;
    GetSystemTimeAsFileTime((FILETIME*) &FileTime);
    return NS100_TO_MS(FileTime.QuadPart - UNIX_EPOCH_AS_FILE_TIME);
}

EXTERN_C_END
