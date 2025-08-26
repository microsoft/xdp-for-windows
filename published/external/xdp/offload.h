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

static_assert(sizeof(XDP_FRAME_GSO) == 4, "XDP_FRAME_GSO must be exactly 4 bytes");

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

static_assert(sizeof(XDP_FRAME_GRO) == 2, "XDP_FRAME_GRO must be exactly 2 bytes");

typedef struct _XDP_FRAME_GRO_TIMESTAMP {
    union {
        struct {
            UINT32 TcpTimestampDelta;
        } TCP;
    } DUMMYUNIONNAME;
} XDP_FRAME_GRO_TIMESTAMP;

static_assert(sizeof(XDP_FRAME_GRO_TIMESTAMP) == 4, "XDP_FRAME_GRO_TIMESTAMP must be exactly 4 bytes");

typedef struct _XDP_FRAME_TIMESTAMP {
    UINT64 Timestamp;
} XDP_FRAME_TIMESTAMP;

static_assert(sizeof(XDP_FRAME_TIMESTAMP) == 8, "XDP_FRAME_TIMESTAMP must be exactly 8 bytes");

#pragma warning(pop)

EXTERN_C_END
