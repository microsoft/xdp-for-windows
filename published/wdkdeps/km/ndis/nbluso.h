// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/version.h>

EXTERN_C_START

#if NDIS_SUPPORT_NDIS683

//
// IP protocol version encoded in IPVersion field of
// NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO->Transmit.IPVersion
//
#define NDIS_UDP_SEGMENTATION_OFFLOAD_IPV4      0
#define NDIS_UDP_SEGMENTATION_OFFLOAD_IPV6      1

//
// Per-NetBufferList information for UdpSegmentationOffloadInfo.
//
typedef struct _NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO
{
    union
    {
        struct
        {
            ULONG MSS :20;
            ULONG UdpHeaderOffset :10;
            ULONG Reserved :1;
            ULONG IPVersion :1;
        } Transmit;

        PVOID Value;
    };
} NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO, *PNDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO;

#endif // NDIS_SUPPORT_NDIS683

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

