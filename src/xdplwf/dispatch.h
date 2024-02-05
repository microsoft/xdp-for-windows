//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

extern DRIVER_OBJECT *XdpLwfDriverObject;
extern const WCHAR *XDP_LWF_PARAMETERS_KEY;
extern XDP_REG_WATCHER *XdpLwfRegWatcher;

BOOLEAN
XdpLwfFaultInject(
    VOID
    );
