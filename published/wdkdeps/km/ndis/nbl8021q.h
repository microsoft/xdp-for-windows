// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/types.h>
#include <ndis/version.h>
#include <ndis/nbl.h>

EXTERN_C_START

//
//  Per-NBL information for Ieee8021QNetBufferListInfo.
//
typedef struct _NDIS_NET_BUFFER_LIST_8021Q_INFO
{
    union
    {
        struct
        {
            UINT32 UserPriority :3;             // 802.1p priority
            UINT32 CanonicalFormatId :1;        // always 0
            UINT32 VlanId :12;                  // VLAN Identification
            UINT32 Reserved :16;                // set to 0 for ethernet
        } TagHeader;

        struct
        {
            UINT32 UserPriority :3;             // 802.1p priority
            UINT32 CanonicalFormatId :1;        // always 0
            UINT32 VlanId :12;                  // VLAN Identification
            UINT32 WMMInfo :4;
            UINT32 Reserved :12;                // set to 0 for Wireless LAN

        } WLanTagHeader;

        PVOID Value;
    };
} NDIS_NET_BUFFER_LIST_8021Q_INFO, *PNDIS_NET_BUFFER_LIST_8021Q_INFO;

#ifdef NDIS_INCLUDE_LEGACY_NAMES
#define NDIS_GET_NET_BUFFER_LIST_VLAN_ID(_NBL) \
    (((NDIS_NET_BUFFER_LIST_8021Q_INFO *) &(_NBL)->NetBufferListInfo[Ieee8021QNetBufferListInfo])->TagHeader.VlanId)
#define NDIS_SET_NET_BUFFER_LIST_VLAN_ID(_NBL, _VlanId)  \
    ((NDIS_NET_BUFFER_LIST_8021Q_INFO *) &(_NBL)->NetBufferListInfo[Ieee8021QNetBufferListInfo])->TagHeader.VlanId = (_VlanId)
#else // NDIS_INCLUDE_LEGACY_NAMES
inline
UINT32
NDIS_GET_NET_BUFFER_LIST_VLAN_ID(
    _In_ NET_BUFFER_LIST const                 *Nbl
    )
{
    NDIS_NET_BUFFER_LIST_8021Q_INFO const *Info = (NDIS_NET_BUFFER_LIST_8021Q_INFO const *)
        &Nbl->NetBufferListInfo[Ieee8021QNetBufferListInfo];
    return Info->TagHeader.VlanId;
}

inline
void
NDIS_SET_NET_BUFFER_LIST_VLAN_ID(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ UINT32                                 VlanId
    )
{
    NDIS_NET_BUFFER_LIST_8021Q_INFO *Info = (NDIS_NET_BUFFER_LIST_8021Q_INFO *)
        &Nbl->NetBufferListInfo[Ieee8021QNetBufferListInfo];
    Info->TagHeader.VlanId = VlanId;
}
#endif // NDIS_INCLUDE_LEGACY_NAMES

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

