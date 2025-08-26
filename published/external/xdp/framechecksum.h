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

typedef struct _XDP_FRAME_CHECKSUM {
    // One of XDP_FRAME_TX_CHECKSUM_ACTION or XDP_FRAME_RX_CHECKSUM_EVALUATION
    UINT8 Layer3 : 2;

    // One of XDP_FRAME_TX_CHECKSUM_ACTION or XDP_FRAME_RX_CHECKSUM_EVALUATION
    UINT8 Layer4 : 2;

    UINT8 Reserved : 4;
} XDP_FRAME_CHECKSUM;

#ifdef __cplusplus
static_assert(sizeof(XDP_FRAME_CHECKSUM) == 1, "XDP_FRAME_CHECKSUM must be exactly 1 byte");
#else
_Static_assert(sizeof(XDP_FRAME_CHECKSUM) == 1, "XDP_FRAME_CHECKSUM must be exactly 1 byte");
#endif

#pragma warning(pop)

EXTERN_C_END
