//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

//
// This header declares the private IOCTL interface for the XDP driver.
//

#include <afxdp.h>
#include <xdp/program.h>
#include <xdpifmode.h>

#define XDP_DEVICE_NAME L"\\Device\\xdp"

#define XDP_OPEN_PACKET_NAME "XdpOpenPacket000"

//
// Type of XDP object to create or open.
//
typedef enum _XDP_OBJECT_TYPE {
    XDP_OBJECT_TYPE_PROGRAM,
    XDP_OBJECT_TYPE_XSK,
    XDP_OBJECT_TYPE_RSS,
} XDP_OBJECT_TYPE;

//
// XDP open packet, which is our common header for NtCreateFile extended
// attributes.
//
typedef struct _XDP_OPEN_PACKET {
    UINT16 MajorVersion;
    UINT16 MinorVersion;
    XDP_OBJECT_TYPE ObjectType;
} XDP_OPEN_PACKET;

//
// Parameters for creating an XDP_OBJECT_TYPE_PROGRAM.
//
typedef struct _XDP_PROGRAM_OPEN {
    UINT32 IfIndex;
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
    UINT32 Flags;
    UINT32 RuleCount;
    CONST XDP_RULE *Rules;
} XDP_PROGRAM_OPEN;

//
// Parameters for creating an XDP_OBJECT_TYPE_RSS.
//
typedef struct _XDP_RSS_OPEN {
    UINT32 IfIndex;
} XDP_RSS_OPEN;

//
// IOCTLs supported by an RSS file handle.
//
#define IOCTL_RSS_GET \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_RSS_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Define IOCTLs supported by an XSK file handle.
//

#define IOCTL_XSK_BIND \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_GET_SOCKOPT \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_SET_SOCKOPT \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_NOTIFY \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Input struct for IOCTL_XSK_BIND
//
typedef struct _XSK_BIND_IN {
    UINT32 IfIndex;
    UINT32 QueueId;
    UINT32 Flags;
    HANDLE SharedUmemSock;
} XSK_BIND_IN;

//
// Input struct for IOCTL_XSK_SET_SOCKOPT
//
typedef struct _XSK_SET_SOCKOPT_IN {
    UINT32 Option;
    UINT32 InputBufferLength;
    const VOID *InputBuffer;
} XSK_SET_SOCKOPT_IN;

//
// Input struct for IOCTL_XSK_NOTIFY
//
typedef struct _XSK_NOTIFY_IN {
    UINT32 Flags;
    UINT32 WaitTimeoutMilliseconds;
} XSK_NOTIFY_IN;
