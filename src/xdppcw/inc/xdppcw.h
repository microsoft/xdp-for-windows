//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define MAXUINT32_STR "4294967295"

typedef struct _PCW_INSTANCE PCW_INSTANCE;

typedef struct DECLSPEC_CACHEALIGN _XDP_PCW_PER_PROCESSOR {
    ULONG MyCounter1;
} XDP_PCW_PER_PROCESSOR;

typedef struct _XDP_PCW_RX_QUEUE {
    UINT64 XskFramesDelivered;
    UINT64 XskFramesDropped;
    UINT64 XskFramesTruncated;
    UINT64 XskInvalidDescriptors;
    UINT64 InspectFramesPassed;
    UINT64 InspectFramesDropped;
    UINT64 InspectFramesRedirected;
    UINT64 InspectFramesForwarded;
} XDP_PCW_RX_QUEUE;

#include <xdppcwcounters.h>
