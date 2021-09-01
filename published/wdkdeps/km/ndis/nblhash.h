// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

EXTERN_C_START

#include <ndis/hashtypes.h>
#include <ndis/nblaccessors.h>

//
// The following macros are used by miniport driver and protocol driver to set and get
// the hash value, hash type and hash function.
//

VOID
inline
NET_BUFFER_LIST_SET_HASH_TYPE(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ ULONG                                  HashType
    )
{
    NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashInfo) =
        UlongToPtr(((PtrToUlong(NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashInfo)) & ~NDIS_HASH_TYPE_MASK) | (HashType & NDIS_HASH_TYPE_MASK)));
}

VOID
inline
NET_BUFFER_LIST_SET_HASH_FUNCTION(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ ULONG                                  HashFunction
    )
{
    NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashInfo) =
        UlongToPtr(((PtrToUlong(NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashInfo)) & ~NDIS_HASH_FUNCTION_MASK) | ((HashFunction) & NDIS_HASH_FUNCTION_MASK)));
}

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NET_BUFFER_LIST_SET_HASH_VALUE(_NBL, _HashValue) \
    (NET_BUFFER_LIST_INFO(_NBL, NetBufferListHashValue) = UlongToPtr(_HashValue))

#define NET_BUFFER_LIST_GET_HASH_TYPE(_NBL) \
    (PtrToUlong(NET_BUFFER_LIST_INFO(_NBL, NetBufferListHashInfo)) & NDIS_HASH_TYPE_MASK)

#define NET_BUFFER_LIST_GET_HASH_FUNCTION(_NBL) \
    (PtrToUlong(NET_BUFFER_LIST_INFO(_NBL, NetBufferListHashInfo)) & (NDIS_HASH_FUNCTION_MASK))

#define NET_BUFFER_LIST_GET_HASH_VALUE(_NBL) \
    (PtrToUlong(NET_BUFFER_LIST_INFO(_NBL, NetBufferListHashValue)))

#else // NDIS_INCLUDE_LEGACY_NAMES

inline
void
NET_BUFFER_LIST_SET_HASH_VALUE(
    _Out_ NET_BUFFER_LIST                      *Nbl,
    _In_ ULONG                                  HashValue
    )
{
    NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashValue) = UlongToPtr(HashValue);
}

inline
ULONG
NET_BUFFER_LIST_GET_HASH_TYPE(
    _In_ NET_BUFFER_LIST const                 *Nbl
    )
{
    return PtrToUlong(NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashInfo)) & NDIS_HASH_TYPE_MASK;
}

inline
ULONG
NET_BUFFER_LIST_GET_HASH_FUNCTION(
    _In_ NET_BUFFER_LIST const                 *Nbl
    )
{
    return PtrToUlong(NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashInfo)) & NDIS_HASH_FUNCTION_MASK;
}

inline
ULONG
NET_BUFFER_LIST_GET_HASH_VALUE(
    _In_ NET_BUFFER_LIST const                 *Nbl
    )
{
    return PtrToUlong(NET_BUFFER_LIST_INFO(Nbl, NetBufferListHashValue));
}

#endif // NDIS_INCLUDE_LEGACY_NAMES

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

