// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region App, Games, or System family
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP | WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_GAMES)

#include <ndis/version.h>
#include <ndis/types.h>
#include <ndis/objectheader.h>
#include <ndis/ndisport.h>
#include <ndis/oidtypes.h>
#include <ndis/nicswitchtypes.h>

EXTERN_C_START

#define NDIS_OID_REQUEST_TIMEOUT_INFINITE       0
#define NDIS_OID_REQUEST_NDIS_RESERVED_SIZE     16

typedef unsigned int UINT;

typedef struct _NDIS_OID_REQUEST
{
    /*
        Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
        Request.Header.Revision = NDIS_OID_REQUEST_REVISION_2;
        Request.Header.Size = sizeof(Request);
    */
    NDIS_OBJECT_HEADER                          Header;
    NDIS_REQUEST_TYPE                           RequestType;
    NDIS_PORT_NUMBER                            PortNumber;    

    // NDIS_OID_REQUEST_TIMEOUT_INFINITE or a quantity in units of Seconds
    UINT                                        Timeout;

    PVOID                                       RequestId;
    NDIS_HANDLE                                 RequestHandle;

    union _REQUEST_DATA
    {
        NDIS_OID                                Oid;

        struct _QUERY
        {
            NDIS_OID                            Oid;
            PVOID                               InformationBuffer;
            UINT                                InformationBufferLength;
            UINT                                BytesWritten;
            UINT                                BytesNeeded;
        } QUERY_INFORMATION;

        struct _SET
        {
            NDIS_OID                            Oid;
            PVOID                               InformationBuffer;
            UINT                                InformationBufferLength;
            UINT                                BytesRead;
            UINT                                BytesNeeded;
        } SET_INFORMATION;

        struct _METHOD
        {
            NDIS_OID                            Oid;
            PVOID                               InformationBuffer;
            ULONG                               InputBufferLength;
            ULONG                               OutputBufferLength;
            ULONG                               MethodId;
            UINT                                BytesWritten;
            UINT                                BytesRead;
            UINT                                BytesNeeded;
        } METHOD_INFORMATION;
    } DATA;

    UCHAR                                       NdisReserved[NDIS_OID_REQUEST_NDIS_RESERVED_SIZE * sizeof(PVOID)];
    UCHAR                                       MiniportReserved[2 * sizeof(PVOID)];
    UCHAR                                       SourceReserved[2 * sizeof(PVOID)];

    UCHAR                                       SupportedRevision;
    UCHAR                                       Reserved1;
    USHORT                                      Reserved2;

#if NDIS_SUPPORT_NDIS650
    NDIS_NIC_SWITCH_ID                          SwitchId;
    NDIS_NIC_SWITCH_VPORT_ID                    VPortId;

#define NDIS_OID_REQUEST_FLAGS_VPORT_ID_VALID   0x0001
    ULONG                                       Flags;
#endif // NDIS_SUPPORT_NDIS650

} NDIS_OID_REQUEST, *PNDIS_OID_REQUEST;

#define NDIS_OID_REQUEST_REVISION_1 1
#define NDIS_SIZEOF_OID_REQUEST_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_OID_REQUEST, Reserved2)

#if NDIS_SUPPORT_NDIS650

#define NDIS_OID_REQUEST_REVISION_2 2
#define NDIS_SIZEOF_OID_REQUEST_REVISION_2 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_OID_REQUEST, Flags)

#endif // NDIS_SUPPORT_NDIS650

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP | WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_GAMES)
#pragma endregion

