//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This header contains definitions for data IO related IOCTLs supported by the
// functional test miniport (xdpfnmp) and the functional test lightweight filter
// (xdpfnlwf).
//

EXTERN_C_START

#ifndef KERNEL_MODE
#include <xdpndisuser.h>
#endif

typedef struct _DATA_BUFFER {
    CONST UCHAR *VirtualAddress;
    UINT32 DataOffset;
    UINT32 DataLength;
    UINT32 BufferLength;
} DATA_BUFFER;

typedef struct _DATA_FRAME {
    DATA_BUFFER *Buffers;
    UINT16 BufferCount;

#pragma warning(push)
#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union
    union {
        //
        // Used when submitting IO.
        //
        struct {
            UINT32 RssHashQueueId;
        } Input;
        //
        // Used when retrieving filtered IO.
        //
        struct {
            PROCESSOR_NUMBER ProcessorNumber;
            UINT32 RssHash;
        } Output;
    };
#pragma warning(pop)
} DATA_FRAME;

typedef struct _DATA_FLUSH_OPTIONS {
    struct {
        UINT32 DpcLevel : 1;
        UINT32 LowResources : 1;
        UINT32 RssCpu : 1;
    } Flags;

    UINT32 RssCpuQueueId;
} DATA_FLUSH_OPTIONS;

//
// Parameters for IOCTL_[RX|TX]_ENQUEUE.
//

typedef struct _DATA_ENQUEUE_IN {
    DATA_FRAME Frame;
} DATA_ENQUEUE_IN;

//
// Parameters for IOCTL_[RX|TX]_FLUSH.
//

typedef struct _DATA_FLUSH_IN {
    DATA_FLUSH_OPTIONS Options;
} DATA_FLUSH_IN;

//
// Parameters for IOCTL_[RX|TX]_FILTER.
//

typedef struct _DATA_FILTER_IN {
    const UCHAR *Pattern;
    const UCHAR *Mask;
    UINT32 Length;
} DATA_FILTER_IN;

//
// Parameters for IOCTL_[RX|TX]_GET_FRAME.
//

typedef struct _DATA_GET_FRAME_IN {
    UINT32 Index;
} DATA_GET_FRAME_IN;

//
// Parameters for IOCTL_[RX|TX]_DEQUEUE_FRAME.
//

typedef struct _DATA_DEQUEUE_FRAME_IN {
    UINT32 Index;
} DATA_DEQUEUE_FRAME_IN;

//
// The OID request interface.
//

typedef enum _FNIO_OID_REQUEST_INTERFACE {
    //
    // The regular, NDIS-serialized OID request interface.
    //
    OID_REQUEST_INTERFACE_REGULAR,

    //
    // The direct OID request interface. These requests are not serialized and
    // can be pended.
    //
    OID_REQUEST_INTERFACE_DIRECT,

    OID_REQUEST_INTERFACE_MAX
} OID_REQUEST_INTERFACE;

//
// Helper type used by parameters for OID related IOCTLs.
//

typedef struct _OID_KEY {
    NDIS_OID Oid;
    NDIS_REQUEST_TYPE RequestType;
    OID_REQUEST_INTERFACE RequestInterface;
} OID_KEY;

EXTERN_C_END
