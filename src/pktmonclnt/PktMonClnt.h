//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// API interface for producing network packet events to PktMon
//

#pragma once

#include "PktMonNpi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _PKTMON_COMPONENT_PROPERTY
{
    PKTMON_COMPONENT_PROPERTY_ID Id;

    union {
        ULONG IfIndex; // PktMonCompProp_IfIndex
        ULONG MiniportIfIndex; // PktMonCompProp_MiniportIfIndex
        ULONG LowerIfIndex; // PktMonCompProp_LowerIfIndex
        ULONG VmsExtIfIndex; // PktMonCompProp_VmsExtIfIndex
        ULONG LowestIfIndex; // PktMonCompProp_LowestIfIndex
        ULONG NdisMedium; // PktMonCompProp_NdisMedium
        ULONG IpIfIndex; // PktMonCompProp_IpIfIndex
        ULONG Vsid; // PktMonCompProp_Vsid
        ULONG Vlan; // PktMonCompProp_Vlan
        ULONG CompartmentId; // PktMonCompProp_CompartmentId

        USHORT OptDataPath; // PktMonCompProp_OptDataPath
        USHORT NdisObject; // PktMonCompProp_NdisObject
        USHORT EtherType; // PktMonCompProp_EtherType

        GUID IfGuid; // PktMonCompProp_IfGuid:

        PKTMON_MAC_ADDRESS MacAddress; // PktMonCompProp_PhysAddress

        CHAR VMSwitchName[PKTMON_MAX_PROPERTY_LENGTH_BYTES]; // PktMonCompProp_VMSwitchName

        SOCKADDR_INET SockAddr; // PktMonCompProp_IpAddress
    };
} PKTMON_COMPONENT_PROPERTY;

typedef struct _PKTMON_COMPONENT_CONTEXT
{
    LIST_ENTRY ListLink;
    LIST_ENTRY EdgeList;
    LONG EdgeCount;

    HANDLE CompHandle;
    PKTMON_COMPONENT_TYPE CompType;
    PKTMON_PACKET_TYPE PacketType;

    INT FlowEnabled : 1;
    INT DropEnabled : 1;

} PKTMON_COMPONENT_CONTEXT;

typedef struct _PKTMON_EDGE_CONTEXT
{
    LIST_ENTRY ListLink;

    HANDLE EdgeHandle;
    PKTMON_COMPONENT_CONTEXT* CompContext;
    PKTMON_PACKET_TYPE PacketType;

} PKTMON_EDGE_CONTEXT;

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI PKTMON_CLIENT_COMP_ENUM)(VOID);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI PKTMON_CLIENT_CLEANUP)(VOID);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI PKTMON_CLIENT_COMP_NOTIFY)(
    _In_ PKTMON_COMPONENT_CONTEXT* CompContext
    );

typedef PKTMON_CLIENT_COMP_ENUM(*PKTMON_CLIENT_COMP_ENUM_HANDLER);
typedef PKTMON_CLIENT_CLEANUP(*PKTMON_CLIENT_CLEANUP_HANDLER);
typedef PKTMON_CLIENT_COMP_NOTIFY(*PKTMON_CLIENT_COMP_NOTIFY_HANDLER);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
(NTAPI *PKTMON_CLNT_INITIALIZE)(
    _In_ PNPI_MODULEID ModuleId,
    _In_ PKTMON_CLIENT_COMP_ENUM_HANDLER EnumComponents,
    _In_opt_ PKTMON_CLIENT_CLEANUP_HANDLER CleanupComponents,
    _In_opt_ PKTMON_CLIENT_COMP_NOTIFY_HANDLER NotifyComponent
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI *PKTMON_CLNT_UNINITIALIZE)();

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
(NTAPI *PKTMON_CLNT_COMPONENT_REGISTER)(
    _Inout_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PCUNICODE_STRING Name,
    _In_ PCUNICODE_STRING Description,
    _In_ PKTMON_COMPONENT_TYPE ComponentType,
    _In_ PKTMON_PACKET_TYPE PacketType
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI *PKTMON_CLNT_COMPONENT_UNREGISTER)(
    _Inout_ PKTMON_COMPONENT_CONTEXT *CompContext
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
(NTAPI *PKTMON_CLNT_SET_COMPONENT_PROPERTY)(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PKTMON_COMPONENT_PROPERTY* CompProperty
    );

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
(NTAPI *PKTMON_CLNT_ADD_EDGE)(
    _In_  PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_  PCUNICODE_STRING Name,
    _In_  PKTMON_PACKET_TYPE PacketType,
    _Out_ PKTMON_EDGE_CONTEXT *EdgeContext
    );

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
(NTAPI *PKTMON_CLNT_NBL_LOG)(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFORMATION *PacketHeaderInformation,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction
    );

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
(NTAPI *PKTMON_CLNT_NBL_DROP)(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFORMATION *PacketHeaderInformation,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode
    );

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
(NTAPI *PKTMON_CLNT_HEADER_INFO_LOG)(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PKTMON_PACKET_HEADER_INFORMATION* PacketHeaderInformation,
    _In_ PKTMON_DIRECTION Direction,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN *Context
    );

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
(NTAPI *PKTMON_CLNT_HEADER_INFO_DROP)(
    _In_ PKTMON_COMPONENT_CONTEXT *CompContext,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_ PKTMON_PACKET_HEADER_INFORMATION *PacketHeaderInformation,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN *Context
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
PktMonClntInitialize(
    _In_ PNPI_MODULEID ModuleId,
    _In_ PKTMON_CLIENT_COMP_ENUM_HANDLER EnumComponents,
    _In_opt_ PKTMON_CLIENT_CLEANUP_HANDLER CleanupComponents,
    _In_opt_ PKTMON_CLIENT_COMP_NOTIFY_HANDLER NotifyComponent
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NTAPI
PktMonClntUninitialize();

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
PktMonClntComponentRegister(
    _Inout_ PKTMON_COMPONENT_CONTEXT* CompContext,
    _In_ PCUNICODE_STRING Name,
    _In_ PCUNICODE_STRING Description,
    _In_ PKTMON_COMPONENT_TYPE Type,
    _In_ PKTMON_PACKET_TYPE PacketType
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
NTAPI
PktMonClntComponentUnregister(
    _Inout_ PKTMON_COMPONENT_CONTEXT* CompContext
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
PktMonClntSetComponentProperty(
    _In_ PKTMON_COMPONENT_CONTEXT* CompContext,
    _In_ PKTMON_COMPONENT_PROPERTY* CompProperty
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
PktMonClntAddEdge(
    _In_  PKTMON_COMPONENT_CONTEXT* CompContext,
    _In_  PCUNICODE_STRING Name,
    _In_  PKTMON_PACKET_TYPE PacketType,
    _Out_ PKTMON_EDGE_CONTEXT* EdgeContext
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntNblLog(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFORMATION* PacketHeaderInformation,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction
    );

//
// DropReason:
//     0 - 0x7FFFFFFF: Reserved for Microsoft.
//     0x80000000 - 0xFFFFFFFF: Free to be used.
//
// LocationCode:
//     0 - 0x7FFFFFFF: Free to be used.
//     0x80000000 - 0xFFFFFFFF: Reserved for Microsoft.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntNblDrop(
    _In_ PKTMON_COMPONENT_CONTEXT* CompContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_opt_ PKTMON_PACKET_HEADER_INFORMATION* PacketHeaderInformation,
    _In_ BOOLEAN UseOnlyFirstNbl,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntHeaderInfoLog(
    _In_ PKTMON_EDGE_CONTEXT* EdgeContext,
    _In_ PKTMON_PACKET_HEADER_INFORMATION* PacketHeaderInformation,
    _In_ PKTMON_DIRECTION Direction,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN* Context
    );

//
// DropReason:
//     0 - 0x7FFFFFFF: Reserved for Microsoft.
//     0x80000000 - 0xFFFFFFFF: Free to be used.
//
// LocationCode:
//     0 - 0x7FFFFFFF: Free to be used.
//     0x80000000 - 0xFFFFFFFF: Reserved for Microsoft.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PktMonClntHeaderInfoDrop(
    _In_ PKTMON_COMPONENT_CONTEXT* CompContext,
    _In_ PKTMON_PACKET_TYPE PacketType,
    _In_ PKTMON_PACKET_HEADER_INFORMATION* PacketHeaderInformation,
    _In_ PKTMON_DIRECTION Direction,
    _In_ INT DropReason,
    _In_ INT LocationCode,
    _In_opt_ PKTMON_PACKET_CONTEXT_IN* Context
    );

//
// Packet monitor NMR client context
//
typedef struct _PKTMON_CLIENT_CONTEXT
{
    HANDLE  NmrClientHandle;

    PEX_RUNDOWN_REF_CACHE_AWARE RundownRef;

    BOOLEAN Enabled;

    PKTMON_CLIENT_COMP_ENUM_HANDLER   EnumComponents;
    PKTMON_CLIENT_CLEANUP_HANDLER     CleanupComponents;
    PKTMON_CLIENT_COMP_NOTIFY_HANDLER NotifyComponent;

    //
    // Provider dispatch
    //
    PVOID ProviderContext;
    PKTMON_PROVIDER_DISPATCH *ProviderDispatch;

} PKTMON_CLIENT_CONTEXT;

extern PKTMON_CLIENT_CONTEXT PktMon;

#ifdef __cplusplus
} /* extern "C" */
#endif

