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
#define XDP_DEVICE_DIRECTORY_NAME L"\\Device\\xdpapi"
#define XDP_PROGRAM_DEVICE_NAME XDP_DEVICE_DIRECTORY_NAME L"\\program"
#define XDP_XSK_DEVICE_NAME XDP_DEVICE_DIRECTORY_NAME L"\\xsk"
#define XDP_INTERFACE_DEVICE_NAME XDP_DEVICE_DIRECTORY_NAME L"\\interface"

#define XDP_OPEN_PACKET_NAME "XdpOpenPacket000"

#define XDP_INFINITE MAXUINT32
#ifdef INFINITE
C_ASSERT(INFINITE == XDP_INFINITE);
#endif

extern CONST GUID DECLSPEC_SELECTANY XDP_DEVICE_CLASS_GUID = { /* 28f93d3f-4c0a-4a7c-8ff1-96b24e19b856 */
    0x28f93d3f,
    0x4c0a,
    0x4a7c,
    {0x8f, 0xf1, 0x96, 0xb2, 0x4e, 0x19, 0xb8, 0x56}
};

extern CONST GUID DECLSPEC_SELECTANY XDP_PROGRAM_DEVICE_CLASS_GUID = { /* 6ad95b14-2cb1-4646-ba32-bc090ab436e5 */
    0x6ad95b14,
    0x2cb1,
    0x4646,
    {0xba, 0x32, 0xbc, 0x09, 0x0a, 0xb4, 0x36, 0xe5}
};

extern CONST GUID DECLSPEC_SELECTANY XDP_XSK_DEVICE_CLASS_GUID = { /* 0903d898-39c3-4a0f-8528-13658fb280f3 */
    0x0903d898,
    0x39c3,
    0x4a0f,
    {0x85, 0x28, 0x13, 0x65, 0x8f, 0xb2, 0x80, 0xf3}
};

extern CONST GUID DECLSPEC_SELECTANY XDP_INTERFACE_DEVICE_CLASS_GUID = { /* 5f1fa9af-e48e-457a-b556-88492b514662 */
    0x5f1fa9af,
    0xe48e,
    0x457a,
    {0xb5, 0x56, 0x88, 0x49, 0x2b, 0x51, 0x46, 0x62}
};

//
// Type of XDP object to create or open.
//
typedef enum _XDP_OBJECT_TYPE {
    XDP_OBJECT_TYPE_PROGRAM,
    XDP_OBJECT_TYPE_XSK,
    XDP_OBJECT_TYPE_INTERFACE,
    XDP_OBJECT_TYPE_MAP,
} XDP_OBJECT_TYPE;

//
// Map an object type to its per-type device name.
//
inline
const WCHAR *
_XdpObjectTypeDeviceName(
    _In_ XDP_OBJECT_TYPE ObjectType
    )
{
    switch (ObjectType) {
    case XDP_OBJECT_TYPE_PROGRAM:   return XDP_PROGRAM_DEVICE_NAME;
    case XDP_OBJECT_TYPE_XSK:       return XDP_XSK_DEVICE_NAME;
    case XDP_OBJECT_TYPE_INTERFACE: return XDP_INTERFACE_DEVICE_NAME;
    default:                        return XDP_DEVICE_NAME;
    }
}

//
// Map an object type to its per-type device class GUID.
//
inline
const GUID *
_XdpObjectTypeDeviceClassGuid(
    _In_ XDP_OBJECT_TYPE ObjectType
    )
{
    switch (ObjectType) {
    case XDP_OBJECT_TYPE_PROGRAM:   return &XDP_PROGRAM_DEVICE_CLASS_GUID;
    case XDP_OBJECT_TYPE_XSK:       return &XDP_XSK_DEVICE_CLASS_GUID;
    case XDP_OBJECT_TYPE_INTERFACE: return &XDP_INTERFACE_DEVICE_CLASS_GUID;
    default:                        return &XDP_DEVICE_CLASS_GUID;
    }
}

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
