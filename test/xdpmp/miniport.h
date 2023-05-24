//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "poll.h"

#define ETH_HDR_LEN 14
#define MAC_ADDR_LEN 6
#define MAX_MULTICAST_ADDRESSES 16
#define MAX_RSS_QUEUES 64
#define MAX_RSS_INDIR_COUNT 128

#define TRY_READ_INT_CONFIGURATION(hConfig, Keyword, pValue) \
    { \
        NDIS_STATUS _Status; \
        PNDIS_CONFIGURATION_PARAMETER Parameter; \
        NdisReadConfiguration(&_Status, &Parameter, (hConfig), &(Keyword), NdisParameterInteger); \
        if (_Status == NDIS_STATUS_SUCCESS) \
        { \
            *(pValue) = Parameter->ParameterData.IntegerData; \
        } \
    }

typedef XdpMpRateSim XDPMP_RATE_SIM_WMI;

typedef enum {
    XDP_STATE_INACTIVE,
    XDP_STATE_ACTIVE,
    XDP_STATE_DELETE_PENDING,
} XDP_QUEUE_STATE;

typedef enum {
    TxSourceNdis,
    TxSourceXdpTx,
    TxSourceXdpRx,
} TX_SOURCE;

typedef struct {
    union {
        NET_BUFFER *Nb;
        XDP_FRAME *Frame;
    };
    TX_SOURCE Source;
} TX_SHADOW_DESCRIPTOR;

typedef struct _ADAPTER_RX_QUEUE ADAPTER_RX_QUEUE;
typedef struct _ADAPTER_TX_QUEUE ADAPTER_TX_QUEUE;

typedef struct _ADAPTER_RX_QUEUE {
    XDP_QUEUE_STATE XdpState;
    BOOLEAN NeedFlush;

    XDP_RX_QUEUE_HANDLE XdpRxQueue;
    XDP_RING *FrameRing;
    XDP_EXTENSION BufferVaExtension;
    XDP_EXTENSION RxActionExtension;

    HW_RING *HwRing;
    UCHAR *BufferArray;
    UINT32 *RecycleArray;
    UINT32 *RxTxArray;
    CONST ADAPTER_TX_QUEUE *Tq;
    UINT32 NumBuffers;
    UINT32 BufferLength;
    UINT32 BufferMask;
    UINT32 DataLength;
    UINT32 PatternLength;
    CONST UCHAR *PatternBuffer;
    UINT32 RecycleIndex;
    UINT32 RxTxIndex;

    UINT32 RateSimFramesAvailable;

    struct {
        UINT64 RxFrames;
        UINT64 RxBytes;
        UINT64 RxDrops;
    } Stats;

    PEX_RUNDOWN_REF_CACHE_AWARE NblRundown;
    NET_BUFFER_LIST **NblArray;
    UINT32 RssHash;

    KEVENT *DeleteComplete;
} ADAPTER_RX_QUEUE;

typedef struct _ADAPTER_TX_QUEUE {
    XDP_QUEUE_STATE XdpState;
    BOOLEAN NeedFlush;

    XDP_TX_QUEUE_HANDLE XdpTxQueue;
    XDP_RING *FrameRing;
    XDP_EXTENSION BufferVaExtension;
    UINT32 XdpHwDescriptorsAvailable;

    HW_RING *HwRing;
    TX_SHADOW_DESCRIPTOR *ShadowRing;
    CONST ADAPTER_RX_QUEUE *Rq;

    UINT32 RateSimFramesAvailable;

    struct {
        UINT64 TxFrames;
        UINT64 TxBytes;
        UINT64 TxDrops;
    } Stats;

    PEX_RUNDOWN_REF_CACHE_AWARE NblRundown;
    UINT32 NbQueueCount;
    NET_BUFFER *NbQueueHead;
    NET_BUFFER **NbQueueTail;
    KSPIN_LOCK NbQueueLock;

    KEVENT *DeleteComplete;
} ADAPTER_TX_QUEUE;

typedef struct DECLSPEC_CACHEALIGN _ADAPTER_QUEUE {
    UINT32 QueueId;

    ADAPTER_RX_QUEUE Rq;
    ADAPTER_TX_QUEUE Tq;

    NDIS_POLL_HANDLE NdisPollHandle;

    //
    // Flag indicating that the test miniport should act as if the underlying
    // hardware is actively processing RX or is idle.
    //
    BOOLEAN HwActiveRx;

    //
    // The native XDP test miniport is capable of generating RX packets and
    // completing TX packets at variable rates. This allows XDPMP to be used
    // for maximum XDP throughput testing (where the XDP platform itself is the
    // bottleneck) and, separately, to evaluate asynchronous completion code
    // paths where the network interface is the primary bottleneck.
    //
    struct {
        BOOLEAN HwArmed;
        NDIS_HANDLE TimerHandle;
        INT64 IntervalQpc;
        INT64 ExpirationQpc;
        INT64 FrequencyQpc;
        UINT32 RxFrameRate;
        UINT32 TxFrameRate;
        PKEVENT CleanupEvent;
    } RateSim;

#if DBG
    LONG ActivePolls;
#endif
    struct _ADAPTER_CONTEXT *Adapter;
} ADAPTER_QUEUE;

typedef struct _ADAPTER_CONTEXT {
    LIST_ENTRY AdapterListLink;

    NDIS_HANDLE MiniportHandle;
    XDP_CAPABILITIES Capabilities;
    XDP_REGISTRATION_HANDLE XdpRegistration;

    PEX_RUNDOWN_REF_CACHE_AWARE NblRundown;
    ULONG RxQSizeExp;
    ULONG MtuSize;
    ULONG RssEnabled;
    ULONG NumRssProcs;
    ULONG NumRssQueues;
    ULONG IndirectionMask;
    ADAPTER_QUEUE *RssQueues;
    ULONG IndirectionTable[MAX_RSS_INDIR_COUNT];

    NDIS_HANDLE RxNblPool;
    UINT32 MdlSize;

    UCHAR MACAddress[MAC_ADDR_LEN];

    NET_IFINDEX IfIndex;

    ULONG CurrentPacketFilter;
    ULONG CurrentLookAhead;
    UCHAR MulticastAddressList[MAC_ADDR_LEN * MAX_MULTICAST_ADDRESSES];
    ULONG NumMulticastAddresses;

    ULONG TxRingSize;
    ULONG TxXdpQosPct;
    ULONG NumRxBuffers;
    ULONG RxBufferLength;
    ULONG RxDataLength;
    ULONG RxPatternLength;
    UCHAR RxPattern[128];
    ULONG RxPatternCopy;
    XDPMP_RATE_SIM_WMI RateSim;
    FNDIS_NPI_CLIENT FndisClient;
    ADAPTER_POLL_PROVIDER PollProvider;
    ADAPTER_POLL_DISPATCH PollDispatch;

} ADAPTER_CONTEXT;

typedef struct {
    EX_PUSH_LOCK Lock;
    LIST_ENTRY AdapterList;
    HANDLE NdisMiniportDriverHandle;
    ULONG NdisVersion;

    NDIS_MEDIUM Medium;
    ULONG PacketFilter;
    ULONG LinkSpeed;
    ULONG64 MaxXmitLinkSpeed;
    ULONG64 XmitLinkSpeed;
    ULONG64 MaxRecvLinkSpeed;
    ULONG64 RecvLinkSpeed;
} GLOBAL_CONTEXT;

typedef struct _MINIPORT_SUPPORTED_XDP_EXTENSIONS {
    XDP_EXTENSION_INFO VirtualAddress;
    XDP_EXTENSION_INFO LogicalAddress;
    XDP_EXTENSION_INFO RxAction;
} MINIPORT_SUPPORTED_XDP_EXTENSIONS;

extern MINIPORT_SUPPORTED_XDP_EXTENSIONS MpSupportedXdpExtensions;

typedef struct _COUNTED_NBL_CHAIN {
    NET_BUFFER_LIST *Head;
    NET_BUFFER_LIST **Tail;
    UINT32 Count;
} COUNTED_NBL_CHAIN;

inline
VOID
CountedNblChainInitialize(
    _Out_ COUNTED_NBL_CHAIN *Chain
    )
{
    Chain->Head = NULL;
    Chain->Tail = &Chain->Head;
    Chain->Count = 0;
}

inline
VOID
CountedNblChainAppend(
    _Inout_ COUNTED_NBL_CHAIN *Chain,
    _In_ NET_BUFFER_LIST *NetBufferList
    )
{
    *Chain->Tail = NetBufferList;
    Chain->Tail = &NetBufferList->Next;
    *Chain->Tail = NULL;
    Chain->Count++;
}
