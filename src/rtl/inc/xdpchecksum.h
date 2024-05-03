//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Provides internet checksum helpers.
//

#pragma once

inline
UINT16
XdpChecksumFold(
    _In_ UINT32 Checksum
    )
{
    Checksum = (UINT16)Checksum + (Checksum >> 16);
    Checksum = (UINT16)Checksum + (Checksum >> 16);

    return (UINT16)Checksum;
}

inline
UINT16
XdpPartialChecksum(
    _In_ CONST VOID *Buffer,
    _In_ UINT16 BufferLength
    )
{
    UINT32 Checksum = 0;
    CONST UINT16 *Buffer16 = (CONST UINT16 *)Buffer;

    while (BufferLength >= sizeof(*Buffer16)) {
        Checksum += *Buffer16++;
        BufferLength -= sizeof(*Buffer16);
    }

    if (BufferLength > 0) {
        Checksum += *(UCHAR *)Buffer16;
    }

    return XdpChecksumFold(Checksum);
}
