//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding

typedef struct _XDP_BUFFER_MDL {
    MDL *Mdl;
    SIZE_T MdlOffset;
} XDP_BUFFER_MDL;

C_ASSERT(sizeof(XDP_BUFFER_MDL) == 2 * sizeof(VOID *));

#pragma warning(pop)

#define XDP_BUFFER_EXTENSION_MDL_NAME L"ms_buffer_mdl"
#define XDP_BUFFER_EXTENSION_MDL_VERSION_1 1U

#include <xdp/datapath.h>
#include <xdp/extension.h>

inline
XDP_BUFFER_MDL *
XdpGetMdlExtension(
    _In_ XDP_BUFFER *Buffer,
    _In_ XDP_EXTENSION *Extension
    )
{
    return (XDP_BUFFER_MDL *)XdpGetExtensionData(Buffer, Extension);
}

EXTERN_C_END
