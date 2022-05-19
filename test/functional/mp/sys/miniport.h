//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define ETH_HDR_LEN 14
#define MAC_ADDR_LEN 6
#define MAX_MULTICAST_ADDRESSES 16

#define MAX_RSS_QUEUES 64

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

typedef struct _ADAPTER_GENERIC ADAPTER_GENERIC;
typedef struct _ADAPTER_NATIVE ADAPTER_NATIVE;
typedef struct _ADAPTER_USER_CONTEXT ADAPTER_USER_CONTEXT;

typedef struct DECLSPEC_CACHEALIGN {
    UINT32 QueueId;
    UINT32 RssHash;
    UINT32 ProcessorIndex;
} ADAPTER_QUEUE;

typedef enum _CHECKSUM_OFFLOAD_STATE {
    ChecksumOffloadDisabled = 0,
    ChecksumOffloadTx = 1,
    ChecksumOffloadRx = 2,
    ChecksumOffloadRxTx = 3,
} CHECKSUM_OFFLOAD_STATE;

C_ASSERT((ChecksumOffloadTx & ChecksumOffloadRx) == 0);
C_ASSERT((ChecksumOffloadTx | ChecksumOffloadRx) == ChecksumOffloadRxTx);

typedef struct _ADAPTER_OFFLOAD {
    CHECKSUM_OFFLOAD_STATE UdpChecksumOffloadIPv4;
} ADAPTER_OFFLOAD;

typedef struct _ADAPTER_CONTEXT {
    LIST_ENTRY AdapterListLink;
    NDIS_HANDLE MiniportHandle;
    NET_IFINDEX IfIndex;

    INT64 ReferenceCount;

    UCHAR MACAddress[MAC_ADDR_LEN];
    ULONG MtuSize;
    ULONG CurrentPacketFilter;
    ULONG CurrentLookAhead;
    UCHAR MulticastAddressList[MAC_ADDR_LEN * MAX_MULTICAST_ADDRESSES];
    ULONG NumMulticastAddresses;
    LARGE_INTEGER LastPauseTimestamp;

    ADAPTER_QUEUE *RssQueues;
    ULONG RssEnabled;
    ULONG NumRssProcs;
    ULONG NumRssQueues;

    UINT32 Encapsulation;
    ADAPTER_OFFLOAD OffloadConfig;
    ADAPTER_OFFLOAD OffloadCapabilities;

    EX_PUSH_LOCK Lock;
    OID_KEY *OidFilterKeys;
    UINT32 OidFilterKeyCount;
    NDIS_OID_REQUEST *FilteredOidRequest;

    //
    // Context for an exclusive user mode handle for configuring the adapter.
    //
    ADAPTER_USER_CONTEXT *UserContext;

    ADAPTER_GENERIC *Generic;
    ADAPTER_NATIVE *Native;
} ADAPTER_CONTEXT;

typedef struct _GLOBAL_CONTEXT {
    EX_PUSH_LOCK Lock;
    LIST_ENTRY AdapterList;
    HANDLE NdisMiniportDriverHandle;
    UINT32 NdisVersion;

    NDIS_MEDIUM Medium;
    ULONG PacketFilter;
    ULONG LinkSpeed;
    ULONG64 MaxXmitLinkSpeed;
    ULONG64 XmitLinkSpeed;
    ULONG64 MaxRecvLinkSpeed;
    ULONG64 RecvLinkSpeed;
} GLOBAL_CONTEXT;

extern GLOBAL_CONTEXT MpGlobalContext;

VOID
MpDereferenceAdapter(
    _In_ ADAPTER_CONTEXT *Adapter
    );

ADAPTER_CONTEXT *
MpFindAdapter(
    _In_ UINT32 IfIndex
    );

NDIS_STATUS
MpSetOffloadParameters(
    _In_ CONST ADAPTER_CONTEXT *Adapter,
    _Inout_ ADAPTER_OFFLOAD *AdapterOffload,
    _In_ CONST NDIS_OFFLOAD_PARAMETERS *OffloadParameters,
    _In_ UINT32 OffloadParametersLength,
    _In_ UINT32 StatusCode
    );
