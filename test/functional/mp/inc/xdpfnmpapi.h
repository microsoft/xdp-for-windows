//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

EXTERN_C_START

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

typedef struct _RX_FRAME {
    UINT16 BufferCount;
    UINT32 RssHashQueueId;
} RX_FRAME;

typedef struct _RX_BUFFER {
    CONST UCHAR *VirtualAddress;
    UINT32 DataOffset;
    UINT32 DataLength;
    UINT32 BufferLength;
} RX_BUFFER;

HRESULT
FnMpRxEnqueue(
    _In_ HANDLE Handle,
    _In_ RX_FRAME *Frame,
    _In_ RX_BUFFER *Buffers
    );

typedef struct _RX_FLUSH_OPTIONS {
    struct {
        UINT32 DpcLevel : 1;
        UINT32 LowResources : 1;
        UINT32 RssCpu : 1;
    } Flags;

    UINT32 RssCpuQueueId;
} RX_FLUSH_OPTIONS;

HRESULT
FnMpRxFlush(
    _In_ HANDLE Handle,
    _In_opt_ RX_FLUSH_OPTIONS *Options
    );

HRESULT
FnMpTxFilter(
    _In_ HANDLE Handle,
    _In_ VOID *Pattern,
    _In_ VOID *Mask,
    _In_ UINT32 Length
    );

typedef struct _TX_BUFFER {
    CONST UCHAR *VirtualAddress;
    UINT32 DataOffset;
    UINT32 DataLength;
    UINT32 BufferLength;
} TX_BUFFER;

typedef struct _TX_FRAME {
    TX_BUFFER *Buffers;
    UINT8 BufferCount;
} TX_FRAME;

HRESULT
FnMpTxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ TX_FRAME *Frame
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

HRESULT
FnMpGetNumActiveRssQueues(
    _In_ HANDLE Handle,
    _Out_ UINT32 *NumQueues
    );

#define FNMP_MIN_MTU 1514
#define FNMP_MAX_MTU (16 * 1024 * 1024)
#define FNMP_DEFAULT_MTU FNMP_MAX_MTU

HRESULT
FnMpSetMtu(
    _In_ HANDLE Handle,
    _In_ UINT32 Mtu
    );

EXTERN_C_END
