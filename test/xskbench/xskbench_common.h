//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <xdpapi.h>
#include <xdpapi_helper.h>
#include <afxdp_helper.h>
#include <afxdp_experimental.h>
#include "cxplat.h"

#define POOLTAG_TXPATTERN           'pTbX' // XbTp
#define POOLTAG_LATSAMPLES          'sLbX' // XbLs
#define POOLTAG_FREERING            'rFbX' // XbFr
#define POOLTAG_QUEUES              'sQbX' // XbQs
#define POOLTAG_THREADS             'sTbX' // XbTs
#define POOLTAG_UMEM                'mUbX' // XbUm
#define POOLTAG_API                 'pAbX' // XbAp

#define STATS_ARRAY_SIZE 60

typedef enum {
    XdpModeSystem,
    XdpModeGeneric,
    XdpModeNative,
} XDP_MODE;

typedef struct _MY_QUEUE {
    INT queueId;
    HANDLE sock;
    HANDLE rxProgram;
    XDP_MODE xdpMode;
    ULONG umemsize;
    ULONG umemchunksize;
    ULONG umemheadroom;
    ULONG txiosize;
    ULONG iobatchsize;
    UINT32 ringsize;
    UCHAR *txPattern;
    UINT32 txPatternLength;
    INT64 *latSamples;
    UINT32 latSamplesCount;
    UINT32 latIndex;
    XSK_POLL_MODE pollMode;
    VOID *freeRingLayout;

    struct {
        BOOLEAN periodicStats : 1;
        BOOLEAN rx : 1;
        BOOLEAN tx : 1;
        BOOLEAN optimizePoking: 1;
        BOOLEAN rxInject : 1;
        BOOLEAN txInspect : 1;
    } flags;

    double statsArray[STATS_ARRAY_SIZE];
    ULONG currStatsArrayIdx;

    ULONGLONG lastTick;
    ULONGLONG packetCount;
    ULONGLONG lastPacketCount;
    ULONGLONG lastRxDropCount;
    ULONGLONG pokesRequestedCount;
    ULONGLONG lastPokesRequestedCount;
    ULONGLONG pokesPerformedCount;
    ULONGLONG lastPokesPerformedCount;

    XSK_RING rxRing;
    XSK_RING txRing;
    XSK_RING fillRing;
    XSK_RING compRing;
    XSK_RING freeRing;
    XSK_UMEM_REG umemReg;
} MY_QUEUE;

VOID
CxPlatXdpApiInitialize(
    _Out_ const XDP_API_CLIENT **XdpApi
    );

VOID
CxPlatXdpApiUninitialize(
    _In_ XDP_API_CLIENT *XdpApi
    );

XDP_STATUS
CxPlatXskCreateEx(
    _In_ XDP_API_CLIENT *Client,
    _Out_ HANDLE *Socket
    );

_Kernel_float_used_
VOID
PrintFinalStats(
    MY_QUEUE *Queue
    );

VOID
CxPlatPrintStats(
    MY_QUEUE *Queue
    );

VOID
CxPlatQueueCleanup(
    _In_ XDP_API_CLIENT *Client,
    MY_QUEUE *Queue
    );

BOOLEAN
CxPlatEnableLargePages(
    VOID
    );

VOID
CxPlatAlignMemory(
    _Inout_ XSK_UMEM_REG *UmemReg
    );


#ifdef __cplusplus
}
#endif