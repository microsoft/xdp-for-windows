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

C_ASSERT(sizeof(XDP_FRAME_CHECKSUM) == 1);

#pragma warning(pop)

#define XDP_FRAME_EXTENSION_CHECKSUM_NAME L"ms_frame_checksum"
#define XDP_FRAME_EXTENSION_CHECKSUM_VERSION_1 1U

#include <xdp/extension.h>

//
// TODO: remove forward declaration after removing explicit offload fields from
// ring descriptors.
//
typedef struct _XDP_FRAME XDP_FRAME;

inline
XDP_FRAME_CHECKSUM *
XdpGetChecksumExtension(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_FRAME_CHECKSUM *)XdpGetExtensionData(Frame, Extension);
}

EXTERN_C_END
