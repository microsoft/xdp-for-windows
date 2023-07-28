//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include <programinspect.h>

typedef struct _XDP_FRAME_WITH_EXTENSIONS {
    XDP_FRAME Frame;
    XDP_BUFFER_VIRTUAL_ADDRESS BufferVirtualAddress;
    XDP_FRAME_FRAGMENT Fragment;
} XDP_FRAME_WITH_EXTENSIONS;

C_ASSERT(
    FIELD_OFFSET(XDP_FRAME_WITH_EXTENSIONS, BufferVirtualAddress) ==
    RTL_SIZEOF_THROUGH_FIELD(XDP_FRAME_WITH_EXTENSIONS, Frame.Buffer));

typedef struct _XDP_FRAME_RING {
    XDP_RING Ring;
    XDP_FRAME_WITH_EXTENSIONS Frames[8];
} XDP_FRAME_RING;

C_ASSERT(
    FIELD_OFFSET(XDP_FRAME_RING, Frames) ==
    RTL_SIZEOF_THROUGH_FIELD(XDP_FRAME_RING, Ring));

typedef struct _XDP_BUFFER_WITH_EXTENSIONS {
    XDP_BUFFER Buffer;
    XDP_BUFFER_VIRTUAL_ADDRESS BufferVirtualAddress;
} XDP_BUFFER_WITH_EXTENSIONS;

C_ASSERT(
    FIELD_OFFSET(XDP_BUFFER_WITH_EXTENSIONS, BufferVirtualAddress) ==
    RTL_SIZEOF_THROUGH_FIELD(XDP_BUFFER_WITH_EXTENSIONS, Buffer));

typedef struct _XDP_FRAGMENT_RING {
    XDP_RING Ring;
    XDP_BUFFER_WITH_EXTENSIONS Buffers[16];
} XDP_FRAGMENT_RING;

C_ASSERT(
    FIELD_OFFSET(XDP_FRAGMENT_RING, Buffers) ==
    RTL_SIZEOF_THROUGH_FIELD(XDP_FRAGMENT_RING, Ring));

typedef struct _PKTFUZZ_BUFFER_METADATA {
    UINT8 DataOffset;
    UINT8 Trailer;
    UINT16 DataLength;
} PKTFUZZ_BUFFER_METADATA;

typedef struct _PKTFUZZ_METADATA {
    UINT32 FragmentRingEnabled : 1;
    UINT32 FrameRingIndex;
    UINT32 FrameRingProducerIndex;
    UINT32 FrameRingConsumerIndex;
    UINT32 FragmentRingIndex;
    UINT32 FragmentRingProducerIndex;
    UINT32 FragmentRingConsumerIndex;
    XDP_RULE Rules[4];
    PKTFUZZ_BUFFER_METADATA FrameBuffer;
    PKTFUZZ_BUFFER_METADATA FragmentBuffers[RTL_NUMBER_OF_FIELD(XDP_FRAGMENT_RING, Buffers)];
} PKTFUZZ_METADATA;

XDP_EXTENSION FragmentExtension = {
    .Reserved = FIELD_OFFSET(XDP_FRAME_WITH_EXTENSIONS, Fragment)
};

XDP_EXTENSION VirtualAddressExtension = {
    .Reserved = FIELD_OFFSET(XDP_BUFFER_WITH_EXTENSIONS, BufferVirtualAddress)
};

#pragma warning(suppress:6262) // Using a LOT of stack space
int
LLVMFuzzerTestOneInput(
    _In_ const UINT8 *Data,
    _In_ SIZE_T Size
    )
{
    NTSTATUS Status;
    int Result;
    const PKTFUZZ_METADATA *Metadata = (const PKTFUZZ_METADATA *)Data;
    XDP_FRAME_RING FrameRing = {
        .Ring.ElementStride = sizeof(FrameRing.Frames[0]),
        .Ring.Mask = RTL_NUMBER_OF(FrameRing.Frames) - 1,
    };
    XDP_FRAGMENT_RING FragmentRing = {
        .Ring.ElementStride = sizeof(FragmentRing.Buffers[0]),
        .Ring.Mask = RTL_NUMBER_OF(FragmentRing.Buffers) - 1,
    };
    XDP_RING *FragmentRingOption = NULL;
    UCHAR ProgramBuffer[FIELD_OFFSET(XDP_PROGRAM, Rules) + sizeof(Metadata->Rules)];
    XDP_PROGRAM *Program = (XDP_PROGRAM *)ProgramBuffer;
    XDP_INSPECTION_CONTEXT InspectionContext = {0};
    UINT32 FrameRingIndex;
    UINT32 FragmentRingIndex = 0;
    XDP_FRAME_WITH_EXTENSIONS *FrameExt = NULL;
    XDP_BUFFER *Buffer;

    if (Size < sizeof(*Metadata)) {
        return -1;
    }

    Data += sizeof(*Metadata);
    Size -= sizeof(*Metadata);

    FrameRingIndex = Metadata->FrameRingIndex & FrameRing.Ring.Mask;
    FrameRing.Ring.ProducerIndex = Metadata->FrameRingProducerIndex;
    FrameRing.Ring.ConsumerIndex = Metadata->FrameRingConsumerIndex;

    if (Size < Metadata->FrameBuffer.DataLength) {
        return -1;
    }

    FrameExt = &FrameRing.Frames[FrameRingIndex];
    Buffer = &FrameExt->Frame.Buffer;

    Buffer->DataLength = Metadata->FrameBuffer.DataLength;
    Buffer->DataOffset = Metadata->FrameBuffer.DataOffset;
    Buffer->BufferLength = Buffer->DataOffset + Buffer->DataLength + Metadata->FrameBuffer.Trailer;

    //
    // Violate the XDP spec in order to catch data under- and over-reads as
    // reliably as possible: the entire buffer should be treated as readable,
    // but since XDP currently does not adjust buffer lengths, treat the
    // backfill and trailer as invalid.
    //
    FrameExt->BufferVirtualAddress.VirtualAddress = malloc(Buffer->DataLength);
    if (FrameExt->BufferVirtualAddress.VirtualAddress == NULL) {
        Result = 0;
        goto Exit;
    }

    RtlCopyMemory(FrameExt->BufferVirtualAddress.VirtualAddress, Data, Buffer->DataLength);
    FrameExt->BufferVirtualAddress.VirtualAddress -= Buffer->DataOffset;

    Data += Buffer->DataLength;
    Size -= Buffer->DataLength;

    if (Metadata->FragmentRingEnabled) {
        FragmentRingIndex = Metadata->FragmentRingIndex & FragmentRing.Ring.Mask;

        FragmentRing.Ring.ProducerIndex = Metadata->FragmentRingProducerIndex;
        FragmentRing.Ring.ConsumerIndex = Metadata->FragmentRingConsumerIndex;
        FragmentRingOption = &FragmentRing.Ring;

        for (UINT32 i = 0; i < RTL_NUMBER_OF(Metadata->FragmentBuffers); i++) {
            XDP_BUFFER_WITH_EXTENSIONS *BufferExt = &FragmentRing.Buffers[FragmentRingIndex];

            if (Size < Metadata->FragmentBuffers[i].DataLength) {
                Result = -1;
                goto Exit;
            }

            Buffer = &BufferExt->Buffer;

            Buffer->DataLength = Metadata->FragmentBuffers[i].DataLength;
            Buffer->DataOffset = Metadata->FragmentBuffers[i].DataOffset;
            Buffer->BufferLength =
                Buffer->DataOffset + Buffer->DataLength + Metadata->FragmentBuffers[i].Trailer;

            BufferExt->BufferVirtualAddress.VirtualAddress = malloc(Buffer->DataLength);
            if (BufferExt->BufferVirtualAddress.VirtualAddress == NULL) {
                Result = 0;
                goto Exit;
            }

            RtlCopyMemory(BufferExt->BufferVirtualAddress.VirtualAddress, Data, Buffer->DataLength);
            BufferExt->BufferVirtualAddress.VirtualAddress -= Buffer->DataOffset;

            Data += Buffer->DataLength;
            Size -= Buffer->DataLength;

            FragmentRingIndex = (FragmentRingIndex + 1) & FragmentRing.Ring.Mask;
        }
    }

    Program->RuleCount = RTL_NUMBER_OF(Metadata->Rules);

    for (UINT32 i = 0; i < Program->RuleCount; i++) {
        Status =
            XdpProgramValidateRule(
                &Program->Rules[i], UserMode, &Metadata->Rules[i], Program->RuleCount, i);

        if (!NT_SUCCESS(Status)) {
            Result = -1;
            goto Exit;
        }
    }

    XdpInspect(
        Program, &InspectionContext, &FrameRing.Ring, FrameRingIndex, FragmentRingOption,
        &FragmentExtension, FragmentRingIndex, &VirtualAddressExtension);

    Result = 0;

Exit:

    for (UINT32 i = 0; i < RTL_NUMBER_OF(FragmentRing.Buffers); i++) {
        XDP_BUFFER_WITH_EXTENSIONS *BufferExt = &FragmentRing.Buffers[i];

        if (BufferExt->BufferVirtualAddress.VirtualAddress != NULL) {
            free(BufferExt->BufferVirtualAddress.VirtualAddress + BufferExt->Buffer.DataOffset);
        }
    }

    if (FrameExt != NULL) {
        if (FrameExt->BufferVirtualAddress.VirtualAddress != NULL) {
            free(FrameExt->BufferVirtualAddress.VirtualAddress + FrameExt->Frame.Buffer.DataOffset);
        }
    }

    return Result;
}

#if _MSC_VER < 1930
int
__cdecl
main()
{
    //
    // Provide a dummy main() function for the VS2019 compiler; it does not
    // support LibFuzzer.
    //
    return 1;
}
#endif
