//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

extern const GUID NPI_XDP_ID;

#define POOLTAG_NMR_CLIENT      'cNdX'   // XdNc
#define POOLTAG_NMR_PROVIDER    'pNdX'   // XdNp

typedef struct _XDP_NPI_CLIENT {
    VOID *InterfaceContext;
    CONST XDP_INTERFACE_DISPATCH *InterfaceDispatch;
} XDP_NPI_CLIENT;

#define XDP_BINDING_VERSION_1 1
