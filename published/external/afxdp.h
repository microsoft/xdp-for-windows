//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef AFXDP_H
#define AFXDP_H

//
// Include all necessary Windows headers first.
//
#include <xdp/wincommon.h>

#include <xdp/apiversion.h>
#include <xdp/hookid.h>
#include <xdp/overlapped.h>
#include <xdp/status.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union
typedef union _XSK_BUFFER_ADDRESS {
    struct {
        UINT64 BaseAddress : 48;
        UINT64 Offset : 16;
    } DUMMYSTRUCTNAME;
    UINT64 AddressAndOffset;
} XSK_BUFFER_ADDRESS;
#pragma warning(pop)

static_assert(sizeof(XSK_BUFFER_ADDRESS) == sizeof(UINT64), "== sizeof(UINT64)");

typedef struct _XSK_BUFFER_DESCRIPTOR {
    XSK_BUFFER_ADDRESS Address;
    UINT32 Length;
    UINT32 Reserved;
} XSK_BUFFER_DESCRIPTOR;

typedef struct _XSK_FRAME_DESCRIPTOR {
    XSK_BUFFER_DESCRIPTOR Buffer;
    //
    // Followed by various descriptor extensions.
    //
} XSK_FRAME_DESCRIPTOR;

//
// Ensure frame-unaware apps can treat the frame ring as a buffer ring.
//
static_assert(FIELD_OFFSET(XSK_FRAME_DESCRIPTOR, Buffer) == 0, "Buffer at offset 0");

typedef enum _XSK_RING_FLAGS {
    XSK_RING_FLAG_NONE = 0x0,
    XSK_RING_FLAG_ERROR = 0x1,
    XSK_RING_FLAG_NEED_POKE = 0x2,
    XSK_RING_FLAG_AFFINITY_CHANGED = 0x4,
    XSK_RING_FLAG_OFFLOAD_CHANGED = 0x8,
} XSK_RING_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_RING_FLAGS);
static_assert(sizeof(XSK_RING_FLAGS) == sizeof(UINT32), "== sizeof(UINT32)");

typedef enum _XSK_BIND_FLAGS {
    XSK_BIND_FLAG_NONE = 0x0,
    XSK_BIND_FLAG_RX = 0x1,
    XSK_BIND_FLAG_TX = 0x2,
    XSK_BIND_FLAG_GENERIC = 0x4,
    XSK_BIND_FLAG_NATIVE = 0x8,
} XSK_BIND_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_BIND_FLAGS)
static_assert(sizeof(XSK_BIND_FLAGS) == sizeof(UINT32), "== sizeof(UINT32)");

typedef enum _XSK_ACTIVATE_FLAGS {
    XSK_ACTIVATE_FLAG_NONE = 0x0,
} XSK_ACTIVATE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_ACTIVATE_FLAGS)
static_assert(sizeof(XSK_ACTIVATE_FLAGS) == sizeof(UINT32), "== sizeof(UINT32)");

typedef enum _XSK_NOTIFY_FLAGS {
    XSK_NOTIFY_FLAG_NONE = 0x0,
    XSK_NOTIFY_FLAG_POKE_RX = 0x1,
    XSK_NOTIFY_FLAG_POKE_TX = 0x2,
    XSK_NOTIFY_FLAG_WAIT_RX = 0x4,
    XSK_NOTIFY_FLAG_WAIT_TX = 0x8,
} XSK_NOTIFY_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_NOTIFY_FLAGS)
static_assert(sizeof(XSK_NOTIFY_FLAGS) == sizeof(UINT32), "== sizeof(UINT32)");

typedef enum _XSK_NOTIFY_RESULT_FLAGS {
    XSK_NOTIFY_RESULT_FLAG_NONE = 0x0,
    XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE = 0x1,
    XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE = 0x2,
} XSK_NOTIFY_RESULT_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XSK_NOTIFY_RESULT_FLAGS)
static_assert(sizeof(XSK_NOTIFY_RESULT_FLAGS) == sizeof(UINT32), "== sizeof(UINT32)");

//
// Socket options
//

#define XSK_SOCKOPT_UMEM_REG 1

typedef struct _XSK_UMEM_REG {
    UINT64 TotalSize;
    UINT32 ChunkSize;
    UINT32 Headroom;
    VOID *Address;
} XSK_UMEM_REG;

#define XSK_SOCKOPT_RX_RING_SIZE              2
#define XSK_SOCKOPT_RX_FILL_RING_SIZE         3
#define XSK_SOCKOPT_TX_RING_SIZE              4
#define XSK_SOCKOPT_TX_COMPLETION_RING_SIZE   5

#define XSK_SOCKOPT_RING_INFO 6

typedef struct _XSK_RING_INFO {
    BYTE *Ring;
    UINT32 DescriptorsOffset;
    UINT32 ProducerIndexOffset;
    UINT32 ConsumerIndexOffset;
    UINT32 FlagsOffset;
    UINT32 Size;
    UINT32 ElementStride;
    UINT32 Reserved;
} XSK_RING_INFO;

typedef struct _XSK_RING_INFO_SET {
    XSK_RING_INFO Fill;
    XSK_RING_INFO Completion;
    XSK_RING_INFO Rx;
    XSK_RING_INFO Tx;
} XSK_RING_INFO_SET;

#define XSK_SOCKOPT_STATISTICS 7

typedef struct _XSK_STATISTICS {
    UINT64 RxDropped;
    UINT64 RxTruncated;
    UINT64 RxInvalidDescriptors;
    UINT64 TxInvalidDescriptors;
} XSK_STATISTICS;

#define XSK_SOCKOPT_RX_HOOK_ID 8
#define XSK_SOCKOPT_TX_HOOK_ID 9

#define XSK_SOCKOPT_RX_ERROR              10
#define XSK_SOCKOPT_RX_FILL_ERROR         11
#define XSK_SOCKOPT_TX_ERROR              12
#define XSK_SOCKOPT_TX_COMPLETION_ERROR   13

typedef enum _XSK_ERROR {
    XSK_NO_ERROR                = 0,
    XSK_ERROR_INTERFACE_DETACH  = 0x80000000,
    XSK_ERROR_INVALID_RING      = 0xC0000000,
} XSK_ERROR;

#define XSK_SOCKOPT_RX_PROCESSOR_AFFINITY 14
#define XSK_SOCKOPT_TX_PROCESSOR_AFFINITY 15

#if !defined(XDP_API_VERSION) || (XDP_API_VERSION <= XDP_API_VERSION_2)
#include <xdp/afxdp_v1.h>
#else

XDP_STATUS
XskCreate(
    _Out_ HANDLE* Socket
    );

XDP_STATUS
XskBind(
    _In_ HANDLE Socket,
    _In_ UINT32 IfIndex,
    _In_ UINT32 QueueId,
    _In_ XSK_BIND_FLAGS Flags
    );

XDP_STATUS
XskActivate(
    _In_ HANDLE Socket,
    _In_ XSK_ACTIVATE_FLAGS Flags
    );

XDP_STATUS
XskSetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    );

XDP_STATUS
XskGetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_opt_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    );

XDP_STATUS
XskIoctl(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(InputLength) const VOID *InputValue,
    _In_ UINT32 InputLength,
    _Out_writes_bytes_opt_(*OutputLength) VOID *OutputValue,
    _Inout_ UINT32 *OutputLength
    );

XDP_STATUS
XskNotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    );

XDP_STATUS
XskNotifyAsync(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _Inout_ XDP_OVERLAPPED *Overlapped
    );

XDP_STATUS
XskGetNotifyAsyncResult(
    _In_ XDP_OVERLAPPED *Overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    );

#include <xdp/details/afxdp.h>

#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
