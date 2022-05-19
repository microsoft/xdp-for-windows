//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _ADAPTER_USER_CONTEXT {
    FILE_OBJECT_HEADER Header;
    ADAPTER_CONTEXT *Adapter;
    BOOLEAN SetOidFilter;
} ADAPTER_USER_CONTEXT;

NTSTATUS
AdapterIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    );
