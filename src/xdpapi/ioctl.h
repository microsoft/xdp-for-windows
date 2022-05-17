//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

//
// This file defines common file handle and IOCTL helpers.
//

//
// This struct is defined in public kernel headers, but not user mode headers.
//
typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} FILE_FULL_EA_INFORMATION;

#define XDP_OPEN_EA_LENGTH \
    (sizeof(FILE_FULL_EA_INFORMATION) + sizeof(XDP_OPEN_PACKET_NAME) + sizeof(XDP_OPEN_PACKET))

VOID *
XdpInitializeEa(
    _In_ XDP_OBJECT_TYPE ObjectType,
    _Out_ VOID *EaBuffer,
    _In_ ULONG EaLength
    );

HANDLE
XdpOpen(
    _In_ ULONG Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ ULONG EaLength
    );

BOOL
XdpOpenGlobal(
    VOID
    );

VOID
XdpCloseGlobal(
    VOID
    );

_Success_(return != FALSE)
BOOL
XdpIoctl(
    _In_ HANDLE XdpHandle,
    _In_ ULONG Operation,
    _In_opt_ VOID *InBuffer,
    _In_ ULONG InBufferSize,
    _Out_opt_ VOID *OutBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_opt_ ULONG *BytesReturned,
    _In_opt_ OVERLAPPED *Overlapped,
    _In_ BOOLEAN MayPend
    );
