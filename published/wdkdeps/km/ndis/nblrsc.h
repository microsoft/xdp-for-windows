// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

EXTERN_C_START

#include <ndis/version.h>
#include <ndis/nblaccessors.h>

#if NDIS_SUPPORT_NDIS630

//
// TcpRecvSegCoalesceInfo
//
typedef union _NDIS_RSC_NBL_INFO
{
    struct
    {
        USHORT CoalescedSegCount;
        USHORT DupAckCount;
    } Info;

    PVOID Value;
} NDIS_RSC_NBL_INFO, *PNDIS_RSC_NBL_INFO;

C_ASSERT(sizeof(NDIS_RSC_NBL_INFO) == sizeof(PVOID));

#if defined(NDIS_INCLUDE_LEGACY_NAMES) || !defined(__cplusplus)

#define NET_BUFFER_LIST_COALESCED_SEG_COUNT(_NBL) \
    (((NDIS_RSC_NBL_INFO*)&NET_BUFFER_LIST_INFO((_NBL), TcpRecvSegCoalesceInfo))->Info.CoalescedSegCount)

#define NET_BUFFER_LIST_DUP_ACK_COUNT(_NBL) \
    (((NDIS_RSC_NBL_INFO*)&NET_BUFFER_LIST_INFO((_NBL), TcpRecvSegCoalesceInfo))->Info.DupAckCount)

#else // NDIS_INCLUDE_LEGACY_NAMES || !__cplusplus

inline
USHORT &
NET_BUFFER_LIST_COALESCED_SEG_COUNT(
    _In_ NET_BUFFER_LIST                       *Nbl
    )
{
    NDIS_RSC_NBL_INFO *Rsc = (NDIS_RSC_NBL_INFO*)&NET_BUFFER_LIST_INFO((Nbl), TcpRecvSegCoalesceInfo);
    return Rsc->Info.CoalescedSegCount;
}

inline
USHORT const &
NET_BUFFER_LIST_COALESCED_SEG_COUNT(
    _In_ NET_BUFFER_LIST const                 *Nbl
    )
{
    NDIS_RSC_NBL_INFO const *Rsc = (NDIS_RSC_NBL_INFO const*)&NET_BUFFER_LIST_INFO((Nbl), TcpRecvSegCoalesceInfo);
    return Rsc->Info.CoalescedSegCount;
}

inline
USHORT &
NET_BUFFER_LIST_DUP_ACK_COUNT(
    _In_ NET_BUFFER_LIST                       *Nbl
    )
{
    NDIS_RSC_NBL_INFO *Rsc = (NDIS_RSC_NBL_INFO*)&NET_BUFFER_LIST_INFO((Nbl), TcpRecvSegCoalesceInfo);
    return Rsc->Info.DupAckCount;
}

inline
USHORT const &
NET_BUFFER_LIST_DUP_ACK_COUNT(
    _In_ NET_BUFFER_LIST const                 *Nbl
    )
{
    NDIS_RSC_NBL_INFO *Rsc = (NDIS_RSC_NBL_INFO const*)&NET_BUFFER_LIST_INFO((Nbl), TcpRecvSegCoalesceInfo);
    return Rsc->Info.DupAckCount;
}

#endif // NDIS_INCLUDE_LEGACY_NAMES || !__cplusplus

#endif // NDIS_SUPPORT_NDIS630

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion
