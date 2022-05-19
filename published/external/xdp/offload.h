//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

//
// NOTE: The definitions in this header are for informational purposes only.
//       All offload structures are currently unsupported by XDP and subject to
//       significant changes.
//

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

C_ASSERT(sizeof(XDP_FRAME_LAYOUT) == 5);

#pragma warning(pop)

typedef enum _XDP_FRAME_TX_CHECKSUM_ACTION {
    XdpFrameTxChecksumActionPassthrough = 0,
    XdpFrameTxChecksumActionRequired = 1,
} XDP_FRAME_TX_CHECKSUM_ACTION;

typedef enum _XDP_FRAME_RX_CHECKSUM_EVALUATION {
    XdpFrameRxChecksumEvaluationNotChecked = 0,
    XdpFrameRxChecksumEvaluationSucceeded = 1,
    XdpFrameRxChecksumEvaluationFailed = 2,
    XdpFrameRxChecksumEvaluationInvalid = 3,
} XDP_FRAME_RX_CHECKSUM_EVALUATION;

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

typedef struct _XDP_FRAME_CHECKSUM {
    // One of XDP_FRAME_TX_CHECKSUM_ACTION or XDP_FRAME_RX_CHECKSUM_EVALUATION
    UINT8 Layer3 : 2;

    // One of XDP_FRAME_TX_CHECKSUM_ACTION or XDP_FRAME_RX_CHECKSUM_EVALUATION
    UINT8 Layer4 : 2;

    UINT8 Reserved : 4;
} XDP_FRAME_CHECKSUM;

C_ASSERT(sizeof(XDP_FRAME_CHECKSUM) == 1);

#pragma warning(pop)

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding
#pragma warning(disable:4214) // nonstandard extension used: bit field types other than int
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union

typedef struct _XDP_FRAME_GSO {
    union {
        struct {
            UINT32 Mss : 20;
            UINT32 Reserved0 : 12;
        } TCP;
        struct {
            UINT32 Mss : 20;
            UINT32 Reserved0 : 12;
        } UDP;
    } DUMMYUNIONNAME;
} XDP_FRAME_GSO;

C_ASSERT(sizeof(XDP_FRAME_GSO) == 4);

#pragma warning(pop)

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union

typedef struct _XDP_FRAME_GRO {
    union {
        struct {
            UINT16 CoalescedSegmentCount;
        } TCP;
        struct {
            UINT16 MessageSize;
        } UDP;
    } DUMMYUNIONNAME;
} XDP_FRAME_GRO;

C_ASSERT(sizeof(XDP_FRAME_GRO) == 2);

typedef struct _XDP_FRAME_GRO_TIMESTAMP {
    union {
        struct {
            UINT32 TcpTimestampDelta;
        } TCP;
    } DUMMYUNIONNAME;
} XDP_FRAME_GRO_TIMESTAMP;

C_ASSERT(sizeof(XDP_FRAME_GRO_TIMESTAMP) == 4);

#pragma warning(pop)

typedef struct _XDP_FRAME_TIMESTAMP {
    UINT64 Timestamp;
} XDP_FRAME_TIMESTAMP;

C_ASSERT(sizeof(XDP_FRAME_TIMESTAMP) == 8);

#pragma warning(pop)

EXTERN_C_END
