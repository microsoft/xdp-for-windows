// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/types.h>
#include <ndis/version.h>
#include <ndis/objectheader.h>
#include <ndis/offloadtypes.h>

EXTERN_C_START

//
// Used in NDIS_STATUS_OFFLOAD_ENCASPULATION_CHANGE status indication and
// OID_OFFLOAD_ENCAPSULATION OID request.
//

#define NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION  0xA8
#define NDIS_OFFLOAD_ENCAPSULATION_REVISION_1   1

typedef struct _NDIS_OFFLOAD_ENCAPSULATION
{
    //
    // Encapsulation.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION;
    // Encapsulation.Header.Size = sizeof(Encapsulation);
    // Encapsulation.Header.Revision = NDIS_OFFLOAD_ENCAPSULATION_REVISION_1;
    //
    NDIS_OBJECT_HEADER                          Header;

    struct
    {
        //
        // A Protocol sets Enable to NDIS_OFFLOAD_SET_ON if it is enabling IPv4
        // LSO, or checksum offloads.  Otherwise it is set to
        // NDIS_OFFLOAD_SET_NO_CHANGE.
        //
        ULONG                                   Enabled;

        //
        // If Enabled is TRUE, a Protocol must set this to either
        // NDIS_ENCAPSULATION_IEEE_802_3 or
        // NDIS_ENCAPSULATION_IEEE_LLC_SNAP_ROUTED.
        //
        ULONG                                   EncapsulationType;

        //
        // If Enabled is TRUE, a protocol must set this field to the header
        // size it uses.
        //
        ULONG                                   HeaderSize;
    } IPv4;

    struct
    {
        //
        // A Protocol sets Enable to NDIS_OFFLOAD_SET_ON if it is enabling IPv6
        // LSO, or checksum offloads.   Otherwise it is set to
        // NDIS_OFFLOAD_SET_NO_CHANGE.
        //
        ULONG                                   Enabled;
        //
        // If Enabled is TRUE, a Protocol must set this to either
        // NDIS_ENCAPSULATION_IEEE_802_3 or
        // NDIS_ENCAPSULATION_IEEE_LLC_SNAP_ROUTED.
        //
        ULONG                                   EncapsulationType;

        //
        // If Enabled is TRUE, a protocol must set this field to the header
        // size it uses.
        //
        ULONG                                   HeaderSize;
    } IPv6;

} NDIS_OFFLOAD_ENCAPSULATION, *PNDIS_OFFLOAD_ENCAPSULATION;

#define NDIS_SIZEOF_OFFLOAD_ENCAPSULATION_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_ENCAPSULATION, IPv6.HeaderSize)

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

