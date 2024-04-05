//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>
#include <bcrypt.h>
#include "platform.h"

typedef VOID _ETHREAD;

typedef struct CX_PLATFORM {

    //
    // Random number algorithm loaded for DISPATCH_LEVEL usage.
    //
    BCRYPT_ALG_HANDLE RngAlgorithm;

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
uint32_t CxPlatProcessorCount;

_IRQL_requires_max_(DISPATCH_LEVEL)
CXPLAT_STATUS
CxPlatRandom(
    _In_ UINT32 BufferLen,
    _Out_writes_bytes_(BufferLen) void* Buffer
    )
{
    //
    // Use the algorithm we initialized for DISPATCH_LEVEL usage.
    //
    CXPLAT_DBG_ASSERT(CxPlatform.RngAlgorithm != NULL);
    return (CXPLAT_STATUS)
        BCryptGenRandom(
            CxPlatform.RngAlgorithm,
            (uint8_t*)Buffer,
            BufferLen,
            0);
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
    CXPLAT_STATUS Status;
    _ETHREAD *EThread = NULL;
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
    CXPLAT_DBG_ASSERT(CXPLAT_SUCCEEDED(Status));
    if (CXPLAT_FAILED(Status)) {
        *Thread = NULL;
        goto Error;
    }
    Status =
        ObReferenceObjectByHandle(
            ThreadHandle,
            THREAD_ALL_ACCESS,
            *PsThreadType,
            KernelMode,
            (void**)&EThread,
            NULL);
    CXPLAT_DBG_ASSERT(CXPLAT_SUCCEEDED(Status));
    if (CXPLAT_FAILED(Status)) {
        *Thread = NULL;
        goto Cleanup;
    }
    PROCESSOR_NUMBER Processor, IdealProcessor;
    Status =
        KeGetProcessorNumberFromIndex(
            Config->IdealProcessor,
            &Processor);
    if (CXPLAT_FAILED(Status)) {
        Status = CXPLAT_STATUS_SUCCESS; // Currently we don't treat this as fatal
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
        CXPLAT_DBG_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
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
        // CXPLAT_DBG_ASSERT(CXPLAT_SUCCEEDED(Status));
        // if (CXPLAT_FAILED(Status)) {
        //     goto Cleanup;
        // }
        // Status =
        //     ZwSetInformationThread(
        //         ThreadHandle,
        //         ThreadGroupInformation,
        //         &Info.NumaNode.GroupMask,
        //         sizeof(GROUP_AFFINITY));
        // CXPLAT_DBG_ASSERT(CXPLAT_SUCCEEDED(Status));
        // if (CXPLAT_FAILED(Status)) {
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
        CXPLAT_DBG_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
    }
SetPriority:
    if (Config->Flags & CXPLAT_THREAD_FLAG_HIGH_PRIORITY) {
        KeSetBasePriorityThread(
            (PKTHREAD)EThread,
            IO_NETWORK_INCREMENT + 1);
    }
    *Thread = (CXPLAT_THREAD)EThread;
Cleanup:
    ZwClose(ThreadHandle);
Error:
    return Status;
}

VOID
CxPlatThreadDelete(
    _In_ CXPLAT_THREAD Thread
    )
{
    _ETHREAD *EThread = (_ETHREAD *)Thread;

    ObDereferenceObject(EThread);
}

BOOLEAN
CxPlatThreadWaitForever(
    _In_ CXPLAT_THREAD Thread
    )
{
    _ETHREAD *EThread = (_ETHREAD *)Thread;

    return KeWaitForSingleObject(EThread, Executive, KernelMode, FALSE, NULL) != STATUS_TIMEOUT;
}

BOOLEAN
CxPlatThreadWait(
    _In_ CXPLAT_THREAD Thread,
    _In_ UINT32 TimeoutMs
    )
{
    _ETHREAD *EThread = (_ETHREAD *)Thread;

    LARGE_INTEGER Timeout100Ns;
    NT_ASSERT(TimeoutMs != MAXUINT32);
    Timeout100Ns.QuadPart = -1 * ((UINT64)TimeoutMs * 10000);
    return KeWaitForSingleObject(EThread, Executive, KernelMode, FALSE, &Timeout100Ns) != STATUS_TIMEOUT;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    void
    )
{
    PAGED_CODE();

    (VOID)KeQueryPerformanceCounter((LARGE_INTEGER*)&CxPlatPerfFreq);

    CxPlatProcessorCount =
        (uint32_t)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    CXPLAT_STATUS Status =
        BCryptOpenAlgorithmProvider(
            &CxPlatform.RngAlgorithm,
            BCRYPT_RNG_ALGORITHM,
            NULL,
            BCRYPT_PROV_DISPATCH);
    if (CXPLAT_FAILED(Status)) {
        goto Error;
    }
    CXPLAT_DBG_ASSERT(CxPlatform.RngAlgorithm != NULL);

Error:

    if (CXPLAT_FAILED(Status)) {
        if (CxPlatform.RngAlgorithm != NULL) {
            BCryptCloseAlgorithmProvider(CxPlatform.RngAlgorithm, 0);
            CxPlatform.RngAlgorithm = NULL;
        }
    }

    return Status;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    )
{
    PAGED_CODE();
    BCryptCloseAlgorithmProvider(CxPlatform.RngAlgorithm, 0);
    CxPlatform.RngAlgorithm = NULL;
}
