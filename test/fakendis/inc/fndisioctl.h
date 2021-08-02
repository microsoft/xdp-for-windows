//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#define FNDIS_DEVICE_NAME L"\\Device\\fndis"

#define IOCTL_FNDIS_POLL_GET_BACKCHANNEL \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x0, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _FNDIS_POLL_GET_BACKCHANNEL {
    VOID *Dispatch;
} FNDIS_POLL_GET_BACKCHANNEL;
