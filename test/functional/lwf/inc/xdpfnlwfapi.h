//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

#include <fniotypes.h>

HRESULT
FnLwfOpenDefault(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

HRESULT
FnLwfTxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    );

HRESULT
FnLwfTxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    );

HRESULT
FnLwfRxFilter(
    _In_ HANDLE Handle,
    _In_ VOID *Pattern,
    _In_ VOID *Mask,
    _In_ UINT32 Length
    );

HRESULT
FnLwfRxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    );

HRESULT
FnLwfRxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    );

HRESULT
FnLwfRxFlush(
    _In_ HANDLE Handle
    );

HRESULT
FnLwfOidSubmitRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    );

EXTERN_C_END
