// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/types.h>
#include <ndis/version.h>

EXTERN_C_START


//
// Receive path
//

//
// The following flags are used in:
//      NdisMIndicateReceiveNetBufferLists
//      NdisFIndicateReceiveNetBufferLists
//      FILTER_RECEIVE_NET_BUFFER_LISTS
//      PROTOCOL_RECEIVE_NET_BUFFER_LISTS
//      and the CoNDIS equivalents
//
#define NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL       0x00000001
#define NDIS_RECEIVE_FLAGS_RESOURCES            0x00000002
#define NDIS_RECEIVE_FLAGS_SINGLE_ETHER_TYPE    0x00000100
#define NDIS_RECEIVE_FLAGS_SINGLE_VLAN          0x00000200
#define NDIS_RECEIVE_FLAGS_PERFECT_FILTERED     0x00000400
#if NDIS_SUPPORT_NDIS620
#define NDIS_RECEIVE_FLAGS_SINGLE_QUEUE         0x00000800
#define NDIS_RECEIVE_FLAGS_SHARED_MEMORY_INFO_VALID 0x00001000
#define NDIS_RECEIVE_FLAGS_MORE_NBLS            0x00002000
#endif // NDIS_SUPPORT_NDIS620
#if NDIS_SUPPORT_NDIS630
#define NDIS_RECEIVE_FLAGS_SWITCH_DESTINATION_GROUP 0x00004000
#define NDIS_RECEIVE_FLAGS_SWITCH_SINGLE_SOURCE 0x00008000
#endif // NDIS_SUPPORT_NDIS630

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NDIS_TEST_RECEIVE_FLAG(_Flags, _Fl)     (((_Flags) & (_Fl)) == (_Fl))
#define NDIS_SET_RECEIVE_FLAG(_Flags, _Fl)      ((_Flags) |= (_Fl))

#define NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(_Flags) \
    NDIS_TEST_RECEIVE_FLAG((_Flags), NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL)

#define NDIS_TEST_RECEIVE_CANNOT_PEND(_Flags) \
    NDIS_TEST_RECEIVE_FLAG((_Flags), NDIS_RECEIVE_FLAGS_RESOURCES)

#define NDIS_TEST_RECEIVE_CAN_PEND(_Flags) \
    (((_Flags) & NDIS_RECEIVE_FLAGS_RESOURCES) == 0)

#endif // NDIS_INCLUDE_LEGACY_NAMES

//
// Receive Return path
//

//
// The following flags are used in:
//      NdisReturnNetBufferLists
//      NdisFReturnNetBufferLists
//      FILTER_RETURN_NET_BUFFER_LISTS
//      MINIPORT_RETURN_NET_BUFFER_LISTS
//
#define NDIS_RETURN_FLAGS_DISPATCH_LEVEL        0x00000001
#if NDIS_SUPPORT_NDIS620
#define NDIS_RETURN_FLAGS_SINGLE_QUEUE          0x00000002
#endif // NDIS_SUPPORT_NDIS620
#if NDIS_SUPPORT_NDIS630
#define NDIS_RETURN_FLAGS_SWITCH_SINGLE_SOURCE  0x00000004
#endif // NDIS_SUPPORT_NDIS630

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NDIS_TEST_RETURN_FLAG(_Flags, _Fl)      (((_Flags) & (_Fl)) == (_Fl))
#define NDIS_SET_RETURN_FLAG(_Flags, _Fl)       ((_Flags) |= (_Fl))

#define NDIS_TEST_RETURN_AT_DISPATCH_LEVEL(_Flags) \
    NDIS_TEST_RETURN_FLAG((_Flags),NDIS_RETURN_FLAGS_DISPATCH_LEVEL)

#endif // NDIS_INCLUDE_LEGACY_NAMES


EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion


