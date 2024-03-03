//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "platform.h"

uint64_t CxPlatPerfFreq;
CXPLAT_PROCESSOR_INFO* CxPlatProcessorInfo;
CXPLAT_PROCESSOR_GROUP_INFO* CxPlatProcessorGroupInfo;
uint32_t CxPlatProcessorCount;

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
XDP_STATUS
CxPlatGetProcessorGroupInfo(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _Outptr_ _At_(*Buffer, __drv_allocatesMem(Mem)) _Pre_defensive_
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Buffer,
    _Out_ PDWORD BufferLength
    );

XDP_STATUS
CxPlatProcessorInfoInit(
    void
    )
{
    XDP_STATUS Status = XDP_STATUS_SUCCESS;
    DWORD InfoLength = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Info = NULL;
    uint32_t ActiveProcessorCount = 0, MaxProcessorCount = 0;
    Status =
        CxPlatGetProcessorGroupInfo(
            RelationGroup,
            &Info,
            &InfoLength);
    if (XDP_FAILED(Status)) {
        goto Error;
    }

    CXPLAT_DBG_ASSERT(InfoLength != 0);
    CXPLAT_DBG_ASSERT(Info->Relationship == RelationGroup);
    CXPLAT_DBG_ASSERT(Info->Group.ActiveGroupCount != 0);
    CXPLAT_DBG_ASSERT(Info->Group.ActiveGroupCount <= Info->Group.MaximumGroupCount);
    if (Info->Group.ActiveGroupCount == 0) {
        Status = XDP_STATUS_INTERNAL_ERROR;
        goto Error;
    }

    for (WORD i = 0; i < Info->Group.ActiveGroupCount; ++i) {
        ActiveProcessorCount += Info->Group.GroupInfo[i].ActiveProcessorCount;
        MaxProcessorCount += Info->Group.GroupInfo[i].MaximumProcessorCount;
    }

    CXPLAT_DBG_ASSERT(ActiveProcessorCount > 0);
    CXPLAT_DBG_ASSERT(ActiveProcessorCount <= UINT16_MAX);
    if (ActiveProcessorCount == 0 || ActiveProcessorCount > UINT16_MAX) {
        Status = XDP_STATUS_INTERNAL_ERROR;
        goto Error;
    }

    CXPLAT_FRE_ASSERT(CxPlatProcessorInfo == NULL);
    CxPlatProcessorInfo =
        CXPLAT_ALLOC_NONPAGED(
            ActiveProcessorCount * sizeof(CXPLAT_PROCESSOR_INFO),
            QUIC_POOL_PLATFORM_PROC);
    if (CxPlatProcessorInfo == NULL) {
        Status = XDP_STATUS_OUT_OF_MEMORY;
        goto Error;
    }

    CXPLAT_DBG_ASSERT(CxPlatProcessorGroupInfo == NULL);
    CxPlatProcessorGroupInfo =
        CXPLAT_ALLOC_NONPAGED(
            Info->Group.ActiveGroupCount * sizeof(CXPLAT_PROCESSOR_GROUP_INFO),
            QUIC_POOL_PLATFORM_PROC);
    if (CxPlatProcessorGroupInfo == NULL) {
        Status = XDP_STATUS_OUT_OF_MEMORY;
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

    Status = XDP_STATUS_SUCCESS;

Error:

    if (Info) {
        CXPLAT_FREE(Info, QUIC_POOL_PLATFORM_TMP_ALLOC);
    }

    if (XDP_FAILED(Status)) {
        if (CxPlatProcessorGroupInfo) {
            CXPLAT_FREE(CxPlatProcessorGroupInfo, QUIC_POOL_PLATFORM_PROC);
            CxPlatProcessorGroupInfo = NULL;
        }
        if (CxPlatProcessorInfo) {
            CXPLAT_FREE(CxPlatProcessorInfo, QUIC_POOL_PLATFORM_PROC);
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
    CXPLAT_FREE(CxPlatProcessorGroupInfo, QUIC_POOL_PLATFORM_PROC);
    CxPlatProcessorGroupInfo = NULL;
    CXPLAT_FREE(CxPlatProcessorInfo, QUIC_POOL_PLATFORM_PROC);
    CxPlatProcessorInfo = NULL;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
XDP_STATUS
CxPlatInitialize(
    void
    )
{
    XDP_STATUS Status;
    BOOLEAN ProcInfoInitialized = FALSE;

    (void)QueryPerformanceFrequency((LARGE_INTEGER*)&CxPlatPerfFreq);

    if (XDP_FAILED(Status = CxPlatProcessorInfoInit())) {
        goto Error;
    }
    ProcInfoInitialized = TRUE;

Error:

    if (XDP_FAILED(Status)) {
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
XDP_STATUS
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

    *Buffer = CXPLAT_ALLOC_NONPAGED(*BufferLength, QUIC_POOL_PLATFORM_TMP_ALLOC);
    if (*Buffer == NULL) {
        return XDP_STATUS_OUT_OF_MEMORY;
    }

    if (!GetLogicalProcessorInformationEx(
            Relationship,
            *Buffer,
            BufferLength)) {
        CXPLAT_FREE(*Buffer, QUIC_POOL_PLATFORM_TMP_ALLOC);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return XDP_STATUS_SUCCESS;
}
