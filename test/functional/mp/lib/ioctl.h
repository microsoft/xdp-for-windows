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

#define XDPFNMP_OPEN_EA_LENGTH \
    (sizeof(FILE_FULL_EA_INFORMATION) + \
        sizeof(XDPFNMP_OPEN_PACKET_NAME) + \
        sizeof(XDPFNMP_OPEN_PACKET))

VOID *
FnMpInitializeEa(
    _In_ XDPFNMP_FILE_TYPE FileType,
    _Out_ VOID *EaBuffer,
    _In_ UINT32 EaLength
    );

HRESULT
FnMpOpen(
    _In_ UINT32 Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ UINT32 EaLength,
    _Out_ HANDLE *Handle
    );

HRESULT
FnMpIoctl(
    _In_ HANDLE XdpHandle,
    _In_ UINT32 Operation,
    _In_opt_ VOID *InBuffer,
    _In_ UINT32 InBufferSize,
    _Out_opt_ VOID *OutBuffer,
    _In_ UINT32 OutputBufferSize,
    _Out_opt_ UINT32 *BytesReturned,
    _In_opt_ OVERLAPPED *Overlapped
    );
