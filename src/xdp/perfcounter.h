//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

NTSTATUS
XdpPerfCountersStart(
    VOID
    );

VOID
XdpPerfCountersStop(
    VOID
    );

typedef struct _MY_COUNTER_SET1_VALUES {
    ULONG MyCounter1;
} MY_COUNTER_SET1_VALUES;

#include <xdppcwcounters.h>
