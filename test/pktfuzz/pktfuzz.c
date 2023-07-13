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

typedef struct _PKTFUZZ_METADATA {
    UINT32 FragmentRingEnabled : 1;
    UINT32 FrameRingIndex;
    UINT32 FrameRingProducerIndex;
    UINT32 FrameRingConsumerIndex;
    UINT32 FragmentRingIndex;
    UINT32 FragmentRingProducerIndex;
    UINT32 FragmentRingConsumerIndex;
    XDP_RULE Rules[4];
} PKTFUZZ_METADATA;

XDP_EXTENSION FragmentExtension = {
    .Reserved = FIELD_OFFSET(XDP_FRAME_WITH_EXTENSIONS, Fragment)
};

XDP_EXTENSION VirtualAddressExtension = {
    .Reserved = FIELD_OFFSET(XDP_BUFFER_WITH_EXTENSIONS, BufferVirtualAddress)
};

int
LLVMFuzzerTestOneInput(
    _In_ const UINT8 *Data,
    _In_ SIZE_T Size
    )
{
    NTSTATUS Status;
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
    UINT32 FragmentRingIndex;

    if (Size < sizeof(*Metadata)) {
        return -1;
    }

    FrameRing.Ring.ProducerIndex = Metadata->FrameRingProducerIndex & FrameRing.Ring.Mask;
    FrameRing.Ring.ConsumerIndex = Metadata->FrameRingConsumerIndex & FrameRing.Ring.Mask;

    if (Metadata->FragmentRingEnabled) {
        FragmentRing.Ring.ProducerIndex = Metadata->FragmentRingProducerIndex & FragmentRing.Ring.Mask;
        FragmentRing.Ring.ConsumerIndex = Metadata->FragmentRingConsumerIndex & FragmentRing.Ring.Mask;
        FragmentRingOption = &FragmentRing.Ring;
    }

    FrameRingIndex = Metadata->FrameRingIndex & FrameRing.Ring.Mask;
    FragmentRingIndex = Metadata->FragmentRingIndex & FragmentRing.Ring.Mask;

    FrameRing.Frames[FrameRingIndex].Frame.Buffer.DataLength = 200;

    Program->RuleCount = RTL_NUMBER_OF(Metadata->Rules);

    for (UINT32 i = 0; i < Program->RuleCount; i++) {
        Status =
            XdpProgramValidateRule(
                &Program->Rules[i], UserMode, &Metadata->Rules[i], Program->RuleCount, i);

        if (!NT_SUCCESS(Status)) {
            return -1;
        }
    }

    XdpInspect(
        Program, &InspectionContext, &FrameRing.Ring, Metadata->FrameRingIndex, FragmentRingOption,
        &FragmentExtension, FragmentRingIndex, &VirtualAddressExtension);

    return 0;
}
