//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef struct _ADAPTER_NATIVE {
    XDP_CAPABILITIES XdpCapabilities;
    ADAPTER_CONTEXT *Adapter;
} ADAPTER_NATIVE;

typedef struct _NATIVE_CONTEXT {
    FILE_OBJECT_HEADER Header;
    LIST_ENTRY ContextListLink;
    EX_PUSH_LOCK Lock;
    XDP_REGISTRATION_HANDLE XdpRegistration;
    ADAPTER_CONTEXT *Adapter;
} NATIVE_CONTEXT;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpXdpNotify(
    _In_ XDP_INTERFACE_HANDLE InterfaceQueue,
    _In_ XDP_NOTIFY_QUEUE_FLAGS Flags
    );
