//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_IOCTLDEF_H
#define XDP_DETAILS_IOCTLDEF_H

#ifdef __cplusplus
extern "C" {
#endif

//
// This header declares the IOCTL interface for the XDP driver.
//

#ifdef _KERNEL_MODE

typedef FILE_FULL_EA_INFORMATION XDP_FILE_FULL_EA_INFORMATION;

#else

//
// This struct is defined in public kernel headers as FILE_FULL_EA_INFORMATION,
// but not user mode headers.
//
typedef struct _XDP_FILE_FULL_EA_INFORMATION {
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} XDP_FILE_FULL_EA_INFORMATION;

#endif

#define XDP_DEVICE_NAME L"\\Device\\xdp"

#define XDP_OPEN_PACKET_NAME "XdpOpenPacket000"

#define XDP_INFINITE MAXUINT32
#ifdef INFINITE
static_assert(INFINITE == XDP_INFINITE, "== XDP_INFINITE");
#endif

extern CONST GUID DECLSPEC_SELECTANY XDP_DEVICE_CLASS_GUID = { /* 28f93d3f-4c0a-4a7c-8ff1-96b24e19b856 */
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
    UINT32 ApiVersion;
    XDP_OBJECT_TYPE ObjectType;
} XDP_OPEN_PACKET;

#define XDP_OPEN_EA_LENGTH \
    (sizeof(XDP_FILE_FULL_EA_INFORMATION) + sizeof(XDP_OPEN_PACKET_NAME) + sizeof(XDP_OPEN_PACKET))

#ifdef __cplusplus
} // extern "C"
#endif

#endif
