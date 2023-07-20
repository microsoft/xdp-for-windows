//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

typedef struct _XDP_BUFFER_LOGICAL_ADDRESS {
    UINT64 LogicalAddress;
} XDP_BUFFER_LOGICAL_ADDRESS;

C_ASSERT(sizeof(XDP_BUFFER_LOGICAL_ADDRESS) == sizeof(VOID *));

#pragma warning(pop)

#define XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_NAME L"ms_buffer_logical_address"
#define XDP_BUFFER_EXTENSION_LOGICAL_ADDRESS_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

inline
XDP_BUFFER_LOGICAL_ADDRESS *
XdpGetLogicalAddressExtension(
    _In_ XDP_BUFFER *Buffer,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_BUFFER_LOGICAL_ADDRESS *)XdpGetExtensionData(Buffer, Extension);
}

EXTERN_C_END
