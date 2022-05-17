//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

//
// Opaque, variable-sized context reserved for use by the XDP interface in each
// XDP buffer; the XDP platform will not read/write this extension.
//
typedef VOID XDP_BUFFER_INTERFACE_CONTEXT;

#define XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME L"ms_buffer_interface_context"
#define XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_VERSION_1 1U

EXTERN_C_END
