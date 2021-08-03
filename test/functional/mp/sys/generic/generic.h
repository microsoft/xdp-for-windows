//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef struct _GENERIC_RX GENERIC_RX;
typedef struct _GENERIC_TX GENERIC_TX;

typedef struct _ADAPTER_GENERIC {
    ADAPTER_CONTEXT *Adapter;
    EX_RUNDOWN_REF NblRundown;
    NDIS_HANDLE NblPool;

    KSPIN_LOCK Lock;
    LIST_ENTRY TxFilterList;
} ADAPTER_GENERIC;

typedef struct _GENERIC_CONTEXT {
    FILE_OBJECT_HEADER Header;
    KSPIN_LOCK Lock;
    ADAPTER_CONTEXT *Adapter;
    GENERIC_RX *Rx;
    GENERIC_TX *Tx;
} GENERIC_CONTEXT;
