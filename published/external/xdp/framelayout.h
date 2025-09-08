//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union
#pragma warning(disable:4214) // nonstandard extension used: bit field types other than int

typedef enum _XDP_FRAME_LAYER2_TYPE {
    XdpFrameLayer2TypeUnspecified,
    XdpFrameLayer2TypeNull,
    XdpFrameLayer2TypeEthernet,
} XDP_FRAME_LAYER2_TYPE;

typedef enum _XDP_FRAME_LAYER3_TYPE {
    XdpFrameLayer3TypeUnspecified,
    XdpFrameLayer3TypeIPv4UnspecifiedOptions,
    XdpFrameLayer3TypeIPv4WithOptions,
    XdpFrameLayer3TypeIPv4NoOptions,
    XdpFrameLayer3TypeIPv6UnspecifiedExtensions,
    XdpFrameLayer3TypeIPv6WithExtensions,
    XdpFrameLayer3TypeIPv6NoExtensions,
} XDP_FRAME_LAYER3_TYPE;

typedef enum _XDP_FRAME_LAYER4_TYPE {
    XdpFrameLayer4TypeUnspecified,
    XdpFrameLayer4TypeTcp,
    XdpFrameLayer4TypeUdp,
    XdpFrameLayer4TypeIPFragment,
    XdpFrameLayer4TypeIPNotFragment,
} XDP_FRAME_LAYER4_TYPE;

#include <pshpack1.h>
typedef struct _XDP_FRAME_LAYOUT {
    UINT16 Layer2HeaderLength : 7;
    UINT16 Layer3HeaderLength : 9;
    UINT8  Layer4HeaderLength : 8;

    // One of the XDP_FRAME_LAYER2_TYPE values
    UINT8 Layer2Type : 4;

    // One of the XDP_FRAME_LAYER3_TYPE values
    UINT8 Layer3Type : 4;

    // One of the XDP_FRAME_LAYER4_TYPE values
    UINT8 Layer4Type : 4;

    UINT8 Reserved0 : 4;
} XDP_FRAME_LAYOUT;
#include <poppack.h>

static_assert(sizeof(XDP_FRAME_LAYOUT) == 5, "XDP_FRAME_LAYOUT must be exactly 5 bytes");

#pragma warning(pop)

EXTERN_C_END
