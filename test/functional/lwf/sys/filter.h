//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef enum _LWF_FILTER_STATE {
    FilterStateUnspecified,
    FilterPaused,
    FilterPausing,
    FilterRunning,
} LWF_FILTER_STATE;

typedef struct _LWF_FILTER {
    NDIS_HANDLE NdisFilterHandle;
    NET_IFINDEX MiniportIfIndex;
    LWF_FILTER_STATE NdisState;
} LWF_FILTER;

NTSTATUS
FilterStart(
    _In_ DRIVER_OBJECT *DriverObject
    );

VOID
FilterStop(
    VOID
    );
