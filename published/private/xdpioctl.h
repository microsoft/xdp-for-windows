//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This header declares the private IOCTL interface for the XDP driver.
//

#include <afxdp.h>
#include <xdpapi.h>
#include <xdpifmode.h>
#include <xdp/program.h>

#define XDP_DEVICE_NAME L"\\Device\\xdp"

#define XDP_OPEN_PACKET_NAME "XdpOpenPacket000"

CONST GUID DECLSPEC_SELECTANY XDP_DEVICE_CLASS_GUID = { /* 28f93d3f-4c0a-4a7c-8ff1-96b24e19b856 */
    0x28f93d3f,
    0x4c0a,
    0x4a7c,
    {0x8f, 0xf1, 0x96, 0xb2, 0x4e, 0x19, 0xb8, 0x56}
};

//
// Type of XDP object to create or open.
//
typedef enum _XDP_OBJECT_TYPE {
    XDP_OBJECT_TYPE_PROGRAM,
    XDP_OBJECT_TYPE_XSK,
    XDP_OBJECT_TYPE_INTERFACE,
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
    XDP_CREATE_PROGRAM_FLAGS Flags;
    UINT32 RuleCount;
    CONST XDP_RULE *Rules;
} XDP_PROGRAM_OPEN;

//
// Parameters for creating an XDP_OBJECT_TYPE_INTERFACE.
//
typedef struct _XDP_INTERFACE_OPEN {
    UINT32 IfIndex;
} XDP_INTERFACE_OPEN;

//
// IOCTLs supported by an interface file handle.
//
#define IOCTL_INTERFACE_OFFLOAD_RSS_GET \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_RSS_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_RSS_GET_CAPABILITIES \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_QEO_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Define IOCTLs supported by an XSK file handle.
//

#define IOCTL_XSK_BIND \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_ACTIVATE \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_GET_SOCKOPT \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_SET_SOCKOPT \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_XSK_NOTIFY \
    CTL_CODE(FILE_DEVICE_NETWORK, 4, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define IOCTL_XSK_NOTIFY_ASYNC \
    CTL_CODE(FILE_DEVICE_NETWORK, 5, METHOD_NEITHER, FILE_WRITE_ACCESS)

//
// Input struct for IOCTL_XSK_BIND
//
typedef struct _XSK_BIND_IN {
    UINT32 IfIndex;
    UINT32 QueueId;
    XSK_BIND_FLAGS Flags;
} XSK_BIND_IN;

//
// Input struct for IOCTL_XSK_ACTIVATE
//
typedef struct _XSK_ACTIVATE_IN {
    XSK_ACTIVATE_FLAGS Flags;
} XSK_ACTIVATE_IN;

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
    XSK_NOTIFY_FLAGS Flags;
    UINT32 WaitTimeoutMilliseconds;
} XSK_NOTIFY_IN;
