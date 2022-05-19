//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <dispatch.h>
#include <miniport.h>

FILE_CREATE_ROUTINE NativeIrpCreate;

NDIS_STATUS
MpNativeStart(
    VOID
    );

VOID
MpNativeCleanup(
    VOID
    );

ADAPTER_NATIVE *
NativeAdapterCreate(
    _In_ ADAPTER_CONTEXT *Adapter
    );

VOID
NativeAdapterCleanup(
    _In_ ADAPTER_NATIVE *AdapterNative
    );

NDIS_STATUS
NativeHandleXdpOid(
    _In_ ADAPTER_NATIVE *AdapterNative,
    _Inout_ VOID *InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _Out_ UINT *BytesNeeded,
    _Out_ UINT *BytesWritten
    );
