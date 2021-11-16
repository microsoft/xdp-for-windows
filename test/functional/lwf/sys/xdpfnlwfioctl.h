//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <xdpfnlwfapi.h>

#define XDPFNLWF_DEVICE_NAME L"\\Device\\xdpfnlwf"

#define XDPFNLWF_OPEN_PACKET_NAME "XdpFnOpenPacket0"

//
// Type of XDP functional test LWF object to create or open.
//
typedef enum _XDPFNLWF_FILE_TYPE {
    XDPFNLWF_FILE_TYPE_DEFAULT,
} XDPFNLWF_FILE_TYPE;

//
// Open packet, the common header for NtCreateFile extended attributes.
//
typedef struct _XDPFNLWF_OPEN_PACKET {
    XDPFNLWF_FILE_TYPE ObjectType;
} XDPFNLWF_OPEN_PACKET;

typedef struct _XDPFNLWF_OPEN_DEFAULT {
    UINT32 IfIndex;
} XDPFNLWF_OPEN_DEFAULT;
