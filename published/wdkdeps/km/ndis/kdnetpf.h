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

#if (NDIS_SUPPORT_NDIS686)

//
// Used to query/add/remove Physical function on a network port.
// These structures are used by these OIDs:
// OID_KDNET_ENUMERATE_PFS
// OID_KDNET_ADD_PF
// OID_KDNET_REMOVE_PF
// OID_KDNET_QUERY_PF_INFORMATION
// These OIDs handle PFs that are primary intended to be used by KDNET.
//

//
// PCI location of the port to query 
//
typedef struct _NDIS_KDNET_BDF
{
    ULONG SegmentNumber;
    ULONG BusNumber;
    ULONG DeviceNumber;
    ULONG FunctionNumber;
    ULONG Reserved;
} NDIS_KDNET_BDF, *PNDIS_KDNET_PCI_BDF;


//
// PF supported states.
//
typedef enum _NDIS_KDNET_PF_STATE
{
    NdisKdNetPfStatePrimary = 0x0,
    NdisKdnetPfStateEnabled = 0x1,
    NdisKdnetPfStateConfigured = 0x2,
} NDIS_KDNET_PF_STATE,*PNDIS_KDNET_PF_STATE;


//
// PF Usage Tag
// Used to indicate the entity that owns the PF.
// Used by the query NdisKdnetQueryUsageTag.
//
typedef enum _NDIS_KDNET_PF_USAGE_TAG
{
    NdisKdnetPfUsageUnknown = 0x0, 
    NdisKdnetPfUsageKdModule = 0x1,
} NDIS_KDNET_PF_USAGE_TAG,*PNDIS_KDNET_PF_USAGE_TAG;


//
// PF element array structure
//
typedef struct _NDIS_KDNET_PF_ENUM_ELEMENT
{
    NDIS_OBJECT_HEADER                          Header;

    //
    // PF value (e.g. if <bus.dev.fun>, then PF value = fun)
    //
    ULONG                                       PfNumber;

    //
    // The PF state value (defined by NDIS_KDNET_PF_STATE)
    //
    NDIS_KDNET_PF_STATE                         PfState;
} NDIS_KDNET_PF_ENUM_ELEMENT, *PNDIS_KDNET_PF_ENUM_ELEMENT;

#define NDIS_KDNET_PF_ENUM_ELEMENT_REVISION_1   1
#define NDIS_SIZEOF_KDNET_PF_ENUM_ELEMENT_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_KDNET_PF_ENUM_ELEMENT, PfState)


//
// This structure describes the data required to enumerate the list of PF
// Used by OID_KDNET_ENUMERATE_PFS.
//
typedef struct _NDIS_KDNET_ENUMERATE_PFS
{
    NDIS_OBJECT_HEADER                          Header;

    //
    // The size of each element is the sizeof(NDIS_KDNET_PF_ENUM_ELEMENT) 
    //
    ULONG                                       ElementSize;

    //
    // The number of elements in the returned array
    //
    ULONG                                       NumberOfElements;

    //
    // Offset value to the first element of the returned array.
    // Each array element is defined by NDIS_KDNET_PF_ENUM_ELEMENT.
    //
    ULONG                                       OffsetToFirstElement;
} NDIS_KDNET_ENUMERATE_PFS, *PNDIS_KDNET_ENUMERATE_PFS;

#define NDIS_KDNET_ENUMERATE_PFS_REVISION_1   1
#define NDIS_SIZEOF_KDNET_ENUMERATE_PFS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_KDNET_ENUMERATE_PFS, OffsetToFirstElement)

//
// This structure indicates the data required to add a PF to the BDF port.
// Used by OID_KDNET_ADD_PF.
//
typedef struct _NDIS_KDNET_ADD_PF
{
    NDIS_OBJECT_HEADER                          Header;

    //
    // One element containing the added PF port number
    //
    ULONG                                       AddedFunctionNumber;
} NDIS_KDNET_ADD_PF, *PNDIS_KDNET_ADD_PF;

#define NDIS_KDNET_ADD_PF_REVISION_1   1
#define NDIS_SIZEOF_KDNET_ADD_PF_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_KDNET_ADD_PF, AddedFunctionNumber)

//
// This structure indicates the data required to remove a PF from the BDF port.
// Used by OID_KDNET_REMOVE_PF.
//
typedef struct _NDIS_KDNET_REMOVE_PF
{

    NDIS_OBJECT_HEADER                          Header;

    //
    // PCI location that points to the PF that needs to be removed
    //
    NDIS_KDNET_BDF                              Bdf;

    //
    // One element containing the removed PF port
    //
    ULONG                                       FunctionNumber;
} NDIS_KDNET_REMOVE_PF, *PNDIS_KDNET_REMOVE_PF;

#define NDIS_KDNET_REMOVE_PF_REVISION_1   1
#define NDIS_SIZEOF_KDNET_REMOVE_PF_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_KDNET_REMOVE_PF, FunctionNumber)

//
// This structure describes the data required to query the PF management data
// Used by OID_KDNET_QUERY_PF_INFORMATION
//
typedef struct _NDIS_KDNET_QUERY_PF_INFORMATION
{
    NDIS_OBJECT_HEADER                          Header;

    //
    // PF PCI location to query for 
    //
    NDIS_KDNET_BDF                              Bdf;

    //
    // PF assigned MAC address
    //
    UCHAR NetworkAdddress[6];

    //
    // PF Usage tag described by NDIS_KDNET_PF_USAGE_TAG
    //
    ULONG UsageTag;

    //
    // Maximum number of Pfs that can be associated to the Primary BDF.
    //
    ULONG MaximumNumberOfSupportedPfs;

    //
    // KDNET PF device ID (Used if there is a new added PF and
    // the FW assigns a new DeviceID to the added KDNET PF)
    //
    ULONG DeviceId;

} NDIS_KDNET_QUERY_PF_INFORMATION, *PNDIS_KDNET_QUERY_PF_INFORMATION;

#define NDIS_KDNET_QUERY_PF_INFORMATION_REVISION_1   1
#define NDIS_SIZEOF_KDNET_QUERY_PF_INFORMATION_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_KDNET_QUERY_PF_INFORMATION, DeviceId)

#endif // (NDIS_SUPPORT_NDIS686)

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion
