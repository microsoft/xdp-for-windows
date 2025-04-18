//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This ringperf microbenchmark measures the performance of AF_XDP/XSK shared
// single producer / single consumer rings.
//

#include <afxdp_helper.h>
#include <intsafe.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

CONST CHAR *UsageText = "Usage: ringperf";
#define REQUIRE(expr) \
    if (!(expr)) { printf("("#expr") failed line %d\n", __LINE__);  exit(1);}

typedef struct _RING_SHARED {
    XSK_RING Ring;
    XSK_RING CompRing;
    UINT64 MaxIterations;
    UINT32 ProdBatchSize;
    UINT32 ConsBatchSize;
} RING_SHARED;

VOID
Usage(
    CHAR *Error
    )
{
    fprintf(stderr, "Error: %s\n%s", Error, UsageText);
}

DWORD
WINAPI
ProdRoutine(
    VOID *Context
    )
{
    RING_SHARED *RingShared = (RING_SHARED *)Context;
    UINT64 Iterations = RingShared->MaxIterations;
    static const UINT32 ProdBaseAddress = 0x12345678;

    while (Iterations > 0) {
        UINT32 ProdIndex;
        UINT32 ProdAvailable;
        UINT32 ConsIndex;
        UINT32 ConsAvailable;

        ProdAvailable =
            XskRingProducerReserve(&RingShared->Ring, RingShared->ProdBatchSize, &ProdIndex);
        for (UINT32 i = 0; i < ProdAvailable; i++) {
            XSK_FRAME_DESCRIPTOR *Frame = XskRingGetElement(&RingShared->Ring, ProdIndex + i);

            Frame->Buffer.Address.BaseAddress = ProdBaseAddress;
            Frame->Buffer.Address.Offset = 2;
            Frame->Buffer.Length = 3;
            Frame->Buffer.Reserved = 0;
        }
        XskRingProducerSubmit(&RingShared->Ring, ProdAvailable);

        ConsAvailable =
            XskRingConsumerReserve(&RingShared->CompRing, RingShared->ProdBatchSize, &ConsIndex);
        for (UINT32 i = 0; i < ConsAvailable; i++) {
            UINT64 *Completion = XskRingGetElement(&RingShared->CompRing, ConsIndex + i);

            REQUIRE(*Completion == ProdBaseAddress)
        }
        XskRingConsumerRelease(&RingShared->CompRing, ConsAvailable);

        Iterations -= ConsAvailable;
    }

    return NO_ERROR;
}

DWORD
WINAPI
ConsRoutine(
    VOID *Context
    )
{
    RING_SHARED *RingShared = (RING_SHARED *)Context;
    UINT64 Iterations = RingShared->MaxIterations;
    static const UINT32 ProdBaseAddress = 0x12345678;

    while (Iterations > 0) {
        UINT32 ConsIndex;
        UINT32 ProdIndex;
        UINT32 Available;

        Available =
            XskRingConsumerReserve(&RingShared->Ring, RingShared->ConsBatchSize, &ConsIndex);
        Available =
            XskRingProducerReserve(&RingShared->CompRing, Available, &ProdIndex);

        for (UINT32 i = 0; i < Available; i++) {
            XSK_FRAME_DESCRIPTOR *Frame = XskRingGetElement(&RingShared->Ring, ConsIndex + i);
            UINT64 *Completion = XskRingGetElement(&RingShared->CompRing, ProdIndex + i);

            *Completion = Frame->Buffer.Address.BaseAddress;
        }

        XskRingConsumerRelease(&RingShared->Ring, Available);
        XskRingProducerSubmit(&RingShared->CompRing, Available);

        Iterations -= Available;
    }

    return NO_ERROR;
}

#pragma warning(push)
#pragma warning(disable:4324) // structure was padded due to alignment specifier

//
// This is based on the shared rings layout provided by xsk.c.
//
typedef struct _RINGPERF_SHARED_RING {
    DECLSPEC_CACHEALIGN UINT32 ProducerIndex;
    DECLSPEC_CACHEALIGN UINT32 ConsumerIndex;
    DECLSPEC_CACHEALIGN UINT32 Flags;
    //
    // Followed by power-of-two array of ring elements, starting on 8-byte alignment.
    // XSK_FRAME_DESCRIPTOR[] for rx/tx, UINT64[] for fill/completion
    //
} RINGPERF_SHARED_RING;

#pragma warning(pop)

static
VOID
AllocateRing(
    _Out_ XSK_RING *Ring,
    _In_ UINT32 ElementSize,
    _In_ UINT32 ElementCount
    )
{
    XSK_RING_INFO RingInfo = {0};
    SIZE_T RingAllocSize;

    REQUIRE(SUCCEEDED(SizeTMult(ElementSize, ElementCount, &RingAllocSize)));
    REQUIRE(SUCCEEDED(SizeTAdd(RingAllocSize, sizeof(RINGPERF_SHARED_RING), &RingAllocSize)));

    RingInfo.Ring = _aligned_malloc(RingAllocSize, SYSTEM_CACHE_ALIGNMENT_SIZE);
    RingInfo.Size = ElementCount;
    RingInfo.DescriptorsOffset = sizeof(RINGPERF_SHARED_RING);
    RingInfo.ConsumerIndexOffset = FIELD_OFFSET(RINGPERF_SHARED_RING, ConsumerIndex);
    RingInfo.ProducerIndexOffset = FIELD_OFFSET(RINGPERF_SHARED_RING, ProducerIndex);
    RingInfo.FlagsOffset = FIELD_OFFSET(RINGPERF_SHARED_RING, Flags);
    RingInfo.ElementStride = ElementSize;

    XskRingInitialize(Ring, &RingInfo);
}

INT
__cdecl
main(
    INT ArgC,
    CHAR **ArgV
    )
{
    INT Err = 0;
    RING_SHARED RingShared = {0};
    HANDLE ProdThread, ConsThread;
    UINT32 ElementCount = 0x1000;

    UNREFERENCED_PARAMETER(ArgC);
    UNREFERENCED_PARAMETER(ArgV);

    RingShared.MaxIterations = 0x100000000ui64;
    RingShared.ProdBatchSize = 32;
    RingShared.ConsBatchSize = 32;

    AllocateRing(&RingShared.Ring, sizeof(XSK_FRAME_DESCRIPTOR), ElementCount);
    AllocateRing(&RingShared.CompRing, sizeof(UINT64), ElementCount);

    ProdThread = CreateThread(NULL, 0, ProdRoutine, &RingShared, 0, NULL);
    REQUIRE(ProdThread != NULL);

    ConsThread = CreateThread(NULL, 0, ConsRoutine, &RingShared, 0, NULL);
    REQUIRE(ConsThread != NULL);

    WaitForSingleObject(ConsThread, INFINITE);
    WaitForSingleObject(ProdThread, INFINITE);

    return Err;
}
