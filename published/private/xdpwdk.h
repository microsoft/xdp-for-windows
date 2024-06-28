//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This file contains defines from WDKs newer than the minimum WDK for third
// parties compiling native XDP drivers.
//

#if (!defined(NTDDI_WIN10_CO) || (WDK_NTDDI_VERSION < NTDDI_WIN10_CO))

inline
BOOLEAN
NET_BUFFER_LIST_IS_TCP_RSC_SET(
    _In_ NET_BUFFER_LIST const *Nbl
    )
{
    NDIS_RSC_NBL_INFO const *Info = (NDIS_RSC_NBL_INFO const *)
        &Nbl->NetBufferListInfo[TcpRecvSegCoalesceInfo];
    return ((UINT_PTR)Info->Value & 0xFFFFFFFF) != 0;
}

#endif
