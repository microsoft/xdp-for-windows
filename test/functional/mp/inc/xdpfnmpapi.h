//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <fniotypes.h>

#define FNMP_DEFAULT_RSS_QUEUES 4
#define FNMP_MAX_RSS_INDIR_COUNT 128

HRESULT
FnMpOpenGeneric(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

HRESULT
FnMpOpenNative(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

HRESULT
FnMpOpenAdapter(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

HRESULT
FnMpRxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    );

HRESULT
FnMpRxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    );

HRESULT
FnMpTxFilter(
    _In_ HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    );

HRESULT
FnMpTxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    );

HRESULT
FnMpTxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    );

HRESULT
FnMpTxFlush(
    _In_ HANDLE Handle
    );

HRESULT
FnMpXdpRegister(
    _In_ HANDLE Handle
    );

HRESULT
FnMpXdpDeregister(
    _In_ HANDLE Handle
    );

HRESULT
FnMpGetLastMiniportPauseTimestamp(
    _In_ HANDLE Handle,
    _Out_ LARGE_INTEGER *Timestamp
    );

#define FNMP_MIN_MTU 1514
#define FNMP_MAX_MTU (16 * 1024 * 1024)
#define FNMP_DEFAULT_MTU FNMP_MAX_MTU

HRESULT
FnMpSetMtu(
    _In_ HANDLE Handle,
    _In_ UINT32 Mtu
    );

HRESULT
FnMpOidFilter(
    _In_ HANDLE Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    );

HRESULT
FnMpOidGetRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Out_opt_ VOID *InformationBuffer
    );

HRESULT
FnMpOidCompleteRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _In_ NDIS_STATUS Status,
    _In_opt_ const VOID *InformationBuffer,
    _In_ UINT32 InformationBufferLength
    );

EXTERN_C_END
