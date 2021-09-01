// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

EXTERN_C_START

#include <ndis/version.h>
#include <ndis/nbl.h>

#if NDIS_SUPPORT_NDIS682

typedef struct _NET_BUFFER_LIST_TIMESTAMP
{
    ULONG64 Timestamp;
} NET_BUFFER_LIST_TIMESTAMP, *PNET_BUFFER_LIST_TIMESTAMP;

#if _WIN64

inline
void
NdisSetNblTimestampInfo(
    _Inout_ NET_BUFFER_LIST                    *Nbl,
    _In_ NET_BUFFER_LIST_TIMESTAMP const       *NblTimestamp
    )
{
    *((ULONG64*)&Nbl->NetBufferListInfo[NetBufferListInfoReserved3]) = NblTimestamp->Timestamp;
}

inline
void
NdisGetNblTimestampInfo(
    _In_ NET_BUFFER_LIST const                 *Nbl,
    _Out_ NET_BUFFER_LIST_TIMESTAMP            *NblTimestamp
    )
{
    NblTimestamp->Timestamp = ((NET_BUFFER_LIST_TIMESTAMP*)&Nbl->NetBufferListInfo[NetBufferListInfoReserved3])->Timestamp;
}

inline
void
NdisCopyNblTimestampInfo(
    _Inout_ NET_BUFFER_LIST                    *NblDest,
    _In_ NET_BUFFER_LIST const                 *NblSrc
    )
{
    NblDest->NetBufferListInfo[NetBufferListInfoReserved3] =
        NblSrc->NetBufferListInfo[NetBufferListInfoReserved3];
}

#else // _WIN64

inline
void
NdisSetNblTimestampInfo(
    _Inout_ NET_BUFFER_LIST                    *Nbl,
    _In_ NET_BUFFER_LIST_TIMESTAMP const       *NblTimestamp
    )
{
    ULARGE_INTEGER ul;

    ul.QuadPart = NblTimestamp->Timestamp;

    *((ULONG*)&Nbl->NetBufferListInfo[NetBufferListInfoReserved3]) = ul.LowPart;
    *((ULONG*)&Nbl->NetBufferListInfo[NetBufferListInfoReserved4]) = ul.HighPart;
}

inline
void
NdisGetNblTimestampInfo(
    _In_ NET_BUFFER_LIST const                 *Nbl,
    _Out_ NET_BUFFER_LIST_TIMESTAMP            *NblTimestamp
    )
{
    ULARGE_INTEGER ul;

    ul.LowPart = *((ULONG*)&Nbl->NetBufferListInfo[NetBufferListInfoReserved3]);
    ul.HighPart = *((ULONG*)&Nbl->NetBufferListInfo[NetBufferListInfoReserved4]);

    NblTimestamp->Timestamp = ul.QuadPart;
}

inline
void
NdisCopyNblTimestampInfo(
    _Inout_ NET_BUFFER_LIST                    *NblDest,
    _In_ NET_BUFFER_LIST const                 *NblSrc
    )
{
    NblDest->NetBufferListInfo[NetBufferListInfoReserved3] =
        NblSrc->NetBufferListInfo[NetBufferListInfoReserved3];
    NblDest->NetBufferListInfo[NetBufferListInfoReserved4] =
        NblSrc->NetBufferListInfo[NetBufferListInfoReserved4];
}

#endif // _WIN64

#endif // NDIS_SUPPORT_NDIS682

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

