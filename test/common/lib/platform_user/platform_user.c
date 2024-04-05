//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <bcrypt.h>
#include "platform.h"

typedef struct CX_PLATFORM {

    //
    // Heap used for all allocations.
    //
    HANDLE Heap;

#ifdef DEBUG
    //
    // 1/Denominator of allocations to fail.
    // Negative is Nth allocation to fail.
    //
    int32_t AllocFailDenominator;

    //
    // Count of allocations.
    //
    long AllocCounter;
#endif

} CX_PLATFORM;

CX_PLATFORM CxPlatform;
uint64_t CxPlatPerfFreq;
CXPLAT_PROCESSOR_INFO* CxPlatProcessorInfo;
CXPLAT_PROCESSOR_GROUP_INFO* CxPlatProcessorGroupInfo;
uint32_t CxPlatProcessorCount;

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
CXPLAT_STATUS
CxPlatGetProcessorGroupInfo(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _Outptr_ _At_(*Buffer, __drv_allocatesMem(Mem)) _Pre_defensive_
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Buffer,
    _Out_ PDWORD BufferLength
    );

CXPLAT_STATUS
CxPlatProcessorInfoInit(
    void
    )
{
    CXPLAT_STATUS Status = CXPLAT_STATUS_SUCCESS;
    DWORD InfoLength = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Info = NULL;
    uint32_t ActiveProcessorCount = 0, MaxProcessorCount = 0;
    Status =
        CxPlatGetProcessorGroupInfo(
            RelationGroup,
            &Info,
            &InfoLength);
    if (CXPLAT_FAILED(Status)) {
        goto Error;
    }

    CXPLAT_DBG_ASSERT(InfoLength != 0);
    CXPLAT_DBG_ASSERT(Info->Relationship == RelationGroup);
    CXPLAT_DBG_ASSERT(Info->Group.ActiveGroupCount != 0);
    CXPLAT_DBG_ASSERT(Info->Group.ActiveGroupCount <= Info->Group.MaximumGroupCount);
    if (Info->Group.ActiveGroupCount == 0) {
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    for (WORD i = 0; i < Info->Group.ActiveGroupCount; ++i) {
        ActiveProcessorCount += Info->Group.GroupInfo[i].ActiveProcessorCount;
        MaxProcessorCount += Info->Group.GroupInfo[i].MaximumProcessorCount;
    }

    CXPLAT_DBG_ASSERT(ActiveProcessorCount > 0);
    CXPLAT_DBG_ASSERT(ActiveProcessorCount <= UINT16_MAX);
    if (ActiveProcessorCount == 0 || ActiveProcessorCount > UINT16_MAX) {
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    CXPLAT_FRE_ASSERT(CxPlatProcessorInfo == NULL);
    CxPlatProcessorInfo =
        CXPLAT_ALLOC_NONPAGED(
            ActiveProcessorCount * sizeof(CXPLAT_PROCESSOR_INFO),
            POOL_PLATFORM_PROC);
    if (CxPlatProcessorInfo == NULL) {
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    CXPLAT_DBG_ASSERT(CxPlatProcessorGroupInfo == NULL);
    CxPlatProcessorGroupInfo =
        CXPLAT_ALLOC_NONPAGED(
            Info->Group.ActiveGroupCount * sizeof(CXPLAT_PROCESSOR_GROUP_INFO),
            POOL_PLATFORM_PROC);
    if (CxPlatProcessorGroupInfo == NULL) {
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    CxPlatProcessorCount = 0;
    for (WORD i = 0; i < Info->Group.ActiveGroupCount; ++i) {
        CxPlatProcessorGroupInfo[i].Mask = Info->Group.GroupInfo[i].ActiveProcessorMask;
        CxPlatProcessorGroupInfo[i].Count = Info->Group.GroupInfo[i].ActiveProcessorCount;
        CxPlatProcessorGroupInfo[i].Offset = CxPlatProcessorCount;
        CxPlatProcessorCount += Info->Group.GroupInfo[i].ActiveProcessorCount;
    }

    for (uint32_t Proc = 0; Proc < ActiveProcessorCount; ++Proc) {
        for (WORD Group = 0; Group < Info->Group.ActiveGroupCount; ++Group) {
            if (Proc >= CxPlatProcessorGroupInfo[Group].Offset &&
                Proc < CxPlatProcessorGroupInfo[Group].Offset + Info->Group.GroupInfo[Group].ActiveProcessorCount) {
                CxPlatProcessorInfo[Proc].Group = Group;
                CxPlatProcessorInfo[Proc].Index = (Proc - CxPlatProcessorGroupInfo[Group].Offset);
                break;
            }
        }
    }

    Status = CXPLAT_STATUS_SUCCESS;

Error:

    if (Info) {
        CXPLAT_FREE(Info, POOL_PLATFORM_TMP_ALLOC);
    }

    if (CXPLAT_FAILED(Status)) {
        if (CxPlatProcessorGroupInfo) {
            CXPLAT_FREE(CxPlatProcessorGroupInfo, POOL_PLATFORM_PROC);
            CxPlatProcessorGroupInfo = NULL;
        }
        if (CxPlatProcessorInfo) {
            CXPLAT_FREE(CxPlatProcessorInfo, POOL_PLATFORM_PROC);
            CxPlatProcessorInfo = NULL;
        }
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatProcessorInfoUnInit(
    void
    )
{
    CXPLAT_FREE(CxPlatProcessorGroupInfo, POOL_PLATFORM_PROC);
    CxPlatProcessorGroupInfo = NULL;
    CXPLAT_FREE(CxPlatProcessorInfo, POOL_PLATFORM_PROC);
    CxPlatProcessorInfo = NULL;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
CXPLAT_STATUS
CxPlatRandom(
    _In_ UINT32 BufferLen,
    _Out_writes_bytes_(BufferLen) void* Buffer
    )
{
    //
    // Just use the system-preferred random number generator algorithm.
    //
    return (CXPLAT_STATUS)
        BCryptGenRandom(
            NULL,
            (uint8_t*)Buffer,
            BufferLen,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

//
// Thread
//

CXPLAT_STATUS
CxPlatThreadCreate(
    _In_ CXPLAT_THREAD_CONFIG* Config,
    _Out_ CXPLAT_THREAD* Thread
    )
{
    HANDLE Handle;
    *Thread = NULL;
    Handle =
        CreateThread(
            NULL,
            0,
            Config->Callback,
            Config->Context,
            0,
            NULL);
    if (Handle == NULL) {
        return GetLastError();
    }
    CXPLAT_DBG_ASSERT(Config->IdealProcessor < CxPlatProcCount());
    const CXPLAT_PROCESSOR_INFO* ProcInfo = &CxPlatProcessorInfo[Config->IdealProcessor];
    GROUP_AFFINITY Group = {0};
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_AFFINITIZE) {
        Group.Mask = (KAFFINITY)(1ull << ProcInfo->Index);          // Fixed processor
        Group.Group = ProcInfo->Group;
        SetThreadGroupAffinity(Handle, &Group, NULL);
    } else {
        // Group.Mask = CxPlatProcessorGroupInfo[ProcInfo->Group].Mask;
        // Group.Group = ProcInfo->Group;
        // SetThreadGroupAffinity(Handle, &Group, NULL);
    }
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_IDEAL_PROC) {
        SetThreadIdealProcessor(Handle, ProcInfo->Index);
    }
    if (Config->Flags & CXPLAT_THREAD_FLAG_HIGH_PRIORITY) {
        SetThreadPriority(Handle, THREAD_PRIORITY_HIGHEST);
    }
    *Thread = (CXPLAT_THREAD)Handle;
    return CXPLAT_STATUS_SUCCESS;
}

VOID
CxPlatThreadDelete(
    _In_ CXPLAT_THREAD Thread
    )
{
    HANDLE Handle = (HANDLE)Thread;

    CloseHandle(Handle);
}

BOOLEAN
CxPlatThreadWaitForever(
    _In_ CXPLAT_THREAD Thread
    )
{
    HANDLE Handle = (HANDLE)Thread;

    return WAIT_OBJECT_0 == WaitForSingleObject(Handle, INFINITE);
}

BOOLEAN
CxPlatThreadWait(
    _In_ CXPLAT_THREAD Thread,
    _In_ UINT32 TimeoutMs
    )
{
    HANDLE Handle = (HANDLE)Thread;

    return WAIT_OBJECT_0 == WaitForSingleObject(Handle, TimeoutMs);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    void
    )
{
    CXPLAT_STATUS Status;
    BOOLEAN ProcInfoInitialized = FALSE;

    (void)QueryPerformanceFrequency((LARGE_INTEGER*)&CxPlatPerfFreq);

    if (CXPLAT_FAILED(Status = CxPlatProcessorInfoInit())) {
        goto Error;
    }
    ProcInfoInitialized = TRUE;

Error:

    if (CXPLAT_FAILED(Status)) {
        if (ProcInfoInitialized) {
            CxPlatProcessorInfoUnInit();
        }
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    )
{
    CxPlatProcessorInfoUnInit();
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
CXPLAT_STATUS
CxPlatGetProcessorGroupInfo(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _Outptr_ _At_(*Buffer, __drv_allocatesMem(Mem)) _Pre_defensive_
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Buffer,
    _Out_ PDWORD BufferLength
    )
{
    *BufferLength = 0;
    GetLogicalProcessorInformationEx(Relationship, NULL, BufferLength);
    if (*BufferLength == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *Buffer = CXPLAT_ALLOC_NONPAGED(*BufferLength, POOL_PLATFORM_TMP_ALLOC);
    if (*Buffer == NULL) {
        return CXPLAT_STATUS_FAIL;
    }

    if (!GetLogicalProcessorInformationEx(
            Relationship,
            *Buffer,
            BufferLength)) {
        CXPLAT_FREE(*Buffer, POOL_PLATFORM_TMP_ALLOC);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return CXPLAT_STATUS_SUCCESS;
}
