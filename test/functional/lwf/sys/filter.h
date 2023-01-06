//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef enum _LWF_FILTER_STATE {
    FilterDetached,
    FilterPaused,
    FilterPausing,
    FilterRunning,
} LWF_FILTER_STATE;

typedef struct _LWF_FILTER {
    LIST_ENTRY FilterListLink;
    NDIS_HANDLE NdisFilterHandle;
    NET_IFINDEX MiniportIfIndex;
    LWF_FILTER_STATE NdisState;
    XDP_REFERENCE_COUNT ReferenceCount;
    KSPIN_LOCK Lock;

    EX_RUNDOWN_REF NblRundown;
    EX_RUNDOWN_REF OidRundown;
    NDIS_HANDLE NblPool;
    LIST_ENTRY RxFilterList;
    LIST_ENTRY StatusFilterList;
} LWF_FILTER;

typedef struct _GLOBAL_CONTEXT {
    EX_PUSH_LOCK Lock;
    LIST_ENTRY FilterList;
    HANDLE NdisDriverHandle;
    UINT32 NdisVersion;
} GLOBAL_CONTEXT;

extern GLOBAL_CONTEXT LwfGlobalContext;

NTSTATUS
FilterStart(
    _In_ DRIVER_OBJECT *DriverObject
    );

VOID
FilterStop(
    VOID
    );

VOID
FilterDereferenceFilter(
    _In_ LWF_FILTER *Filter
    );

LWF_FILTER *
FilterFindAndReferenceFilter(
    _In_ UINT32 IfIndex
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
FilterIrpGetDatapathState(
    _In_ LWF_FILTER *Filter,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
