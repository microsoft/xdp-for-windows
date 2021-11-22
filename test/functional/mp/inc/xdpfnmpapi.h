//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

#ifndef _KERNEL_MODE
#include <xdpndisuser.h>
#endif

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

typedef struct _DATA_BUFFER {
    CONST UCHAR *VirtualAddress;
    UINT32 DataOffset;
    UINT32 DataLength;
    UINT32 BufferLength;
} DATA_BUFFER;

typedef struct _DATA_FRAME {
    DATA_BUFFER *Buffers;
    UINT16 BufferCount;
    struct {
        UINT32 RssHashQueueId;
    } Rx;
} DATA_FRAME;

HRESULT
FnMpRxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame,
    _In_ DATA_BUFFER *Buffers
    );

typedef struct _DATA_FLUSH_OPTIONS {
    struct {
        UINT32 DpcLevel : 1;
        UINT32 LowResources : 1;
        UINT32 RssCpu : 1;
    } Flags;

    UINT32 RssCpuQueueId;
} DATA_FLUSH_OPTIONS;

HRESULT
FnMpRxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    );

HRESULT
FnMpTxFilter(
    _In_ HANDLE Handle,
    _In_ VOID *Pattern,
    _In_ VOID *Mask,
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

typedef struct _OID_KEY {
    NDIS_OID Oid;
    NDIS_REQUEST_TYPE RequestType;
} OID_KEY;

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

EXTERN_C_END
