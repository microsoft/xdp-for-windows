// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/version.h>

EXTERN_C_START

//
// NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO.Transmit.Type
//
#define NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE     0
#define NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE     1

//
// NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO.LsoV2Transmit.IPVersion
//
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPv4        0
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPv6        1

//
// The maximum length of the headers (MAC+IP+IP option or extension
// headers+TCP+TCP options) when the protocol does large send offload.  If
// header is bigger than this value, it will not do LSO.
//
#define NDIS_LARGE_SEND_OFFLOAD_MAX_HEADER_LENGTH 128

//
// Per-NetBufferList information for TcpLargeSendNetBufferListInfo.
//
typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO
{
    union
    {
        struct
        {
            ULONG Unused :30;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } Transmit;

        struct
        {
            ULONG MSS :20;
            ULONG TcpHeaderOffset :10;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } LsoV1Transmit;

        struct
        {
            ULONG TcpPayload :30;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } LsoV1TransmitComplete;

        struct
        {
            ULONG MSS :20;
            ULONG TcpHeaderOffset :10;
            ULONG Type :1;
            ULONG IPVersion :1;
        } LsoV2Transmit;

        struct
        {
            ULONG Reserved :30;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } LsoV2TransmitComplete;

        PVOID Value;
    };
} NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO, *PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO;

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

