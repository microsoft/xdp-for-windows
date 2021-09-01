// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/types.h>
#include <ndis/version.h>

EXTERN_C_START


//
// Send path
//

//
// The following flags are used in:
//      NdisSendNetBufferLists
//      NdisFSendNetBufferLists
//      FILTER_SEND_NET_BUFFER_LISTS
//      MINIPORT_SEND_NET_BUFFER_LISTS_HANDLER
//      and the CoNDIS equivalents
//
#define NDIS_SEND_FLAGS_DISPATCH_LEVEL          0x00000001
#define NDIS_SEND_FLAGS_CHECK_FOR_LOOPBACK      0x00000002
#if NDIS_SUPPORT_NDIS620
#define NDIS_SEND_FLAGS_SINGLE_QUEUE            0x00000004
#endif // NDIS_SUPPORT_NDIS620
#if NDIS_SUPPORT_NDIS630
#define NDIS_SEND_FLAGS_SWITCH_DESTINATION_GROUP 0x00000010
#define NDIS_SEND_FLAGS_SWITCH_SINGLE_SOURCE    0x00000020
#endif // NDIS_SUPPORT_NDIS630

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NDIS_TEST_SEND_FLAG(_Flags, _Fl)        (((_Flags) & (_Fl)) == (_Fl))
#define NDIS_SET_SEND_FLAG(_Flags, _Fl)         ((_Flags) |= (_Fl))
#define NDIS_CLEAR_SEND_FLAG(_Flags, _Fl)       ((_Flags) &= ~(_Fl))

#define NDIS_TEST_SEND_AT_DISPATCH_LEVEL(_Flags) \
    NDIS_TEST_SEND_FLAG((_Flags), NDIS_SEND_FLAGS_DISPATCH_LEVEL)

#endif // NDIS_INCLUDE_LEGACY_NAMES

//
// Send Complete path
//

//
// The following flags are used in:
//      NdisMSendNetBufferListsComplete
//      NdisFSetNetBufferListsComplete
//      FILTER_SEND_NET_BUFFER_LISTS_COMPLETE
//      PROTOCOL_SEND_NET_BUFFER_LISTS_COMPLETE
//      and the CoNDIS equivalents
//
#define NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL 0x00000001
#if NDIS_SUPPORT_NDIS620
#define NDIS_SEND_COMPLETE_FLAGS_SINGLE_QUEUE   0x00000002
#endif // NDIS_SUPPORT_NDIS620
#if NDIS_SUPPORT_NDIS630
#define NDIS_SEND_COMPLETE_FLAGS_SWITCH_SINGLE_SOURCE 0x00000004
#endif // NDIS_SUPPORT_NDIS630

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NDIS_TEST_SEND_COMPLETE_FLAG(_Flags, _Fl) \
    (((_Flags) & (_Fl)) == (_Fl))
#define NDIS_SET_SEND_COMPLETE_FLAG(_Flags, _Fl) \
    ((_Flags) |= (_Fl))

#define NDIS_TEST_SEND_COMPLETE_AT_DISPATCH_LEVEL(_Flags) \
    NDIS_TEST_SEND_COMPLETE_FLAG((_Flags), NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL)

#endif // NDIS_INCLUDE_LEGACY_NAMES


EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion


