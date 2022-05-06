//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "dispatch.h"

typedef struct _LWF_FILTER LWF_FILTER;
typedef struct _DEFAULT_RX DEFAULT_RX;
typedef struct _DEFAULT_TX DEFAULT_TX;
typedef struct _DEFAULT_STATUS DEFAULT_STATUS;

typedef struct _DEFAULT_CONTEXT {
    FILE_OBJECT_HEADER Header;
    KSPIN_LOCK Lock;
    LWF_FILTER *Filter;
    DEFAULT_RX *Rx;
    DEFAULT_TX *Tx;
    DEFAULT_STATUS *Status;
} DEFAULT_CONTEXT;

FILE_CREATE_ROUTINE DefaultIrpCreate;
