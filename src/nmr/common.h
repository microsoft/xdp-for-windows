//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

extern const GUID NPI_XDP_ID;

#define POOLTAG_NMR_CLIENT      'cNdX'   // XdNc
#define POOLTAG_NMR_PROVIDER    'pNdX'   // XdNp

typedef struct _XDP_NPI_CLIENT {
    XDP_OBJECT_HEADER Header;
    VOID *GetInterfaceContext;
    XDP_GET_INTERFACE_DISPATCH *GetInterfaceDispatch;
} XDP_NPI_CLIENT;

#define XDP_NPI_CLIENT_REVISION_1 1

#define XDP_SIZEOF_NPI_CLIENT_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_NPI_CLIENT, GetInterfaceDispatch)

#define XDP_BINDING_VERSION_1 1
