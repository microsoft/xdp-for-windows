// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/types.h>
#include <ndis/version.h>
#include <ndis/nblinfo.h>

EXTERN_C_START


#pragma warning(push)
#pragma warning(disable:4201) // (nonstandard extension used : nameless struct/union)

typedef struct _NET_BUFFER NET_BUFFER, *PNET_BUFFER;
typedef struct _NET_BUFFER_LIST_CONTEXT NET_BUFFER_LIST_CONTEXT, *PNET_BUFFER_LIST_CONTEXT;
typedef struct _NET_BUFFER_LIST NET_BUFFER_LIST, *PNET_BUFFER_LIST;

typedef struct _SCATTER_GATHER_LIST SCATTER_GATHER_LIST;
typedef struct _MDL MDL;

//
// These types are no longer needed; they exist for source-compatibility with
// older versions of NDIS.H.
//

#ifdef NDIS_INCLUDE_LEGACY_NAMES

typedef union _NET_BUFFER_DATA_LENGTH
{
    ULONG DataLength;
    SIZE_T stDataLength;
} NET_BUFFER_DATA_LENGTH, *PNET_BUFFER_DATA_LENGTH;

typedef struct _NET_BUFFER_DATA
{
    NET_BUFFER *Next;
    MDL *CurrentMdl;
    ULONG CurrentMdlOffset;
#ifdef __cplusplus
    NET_BUFFER_DATA_LENGTH NbDataLength;
#else
    NET_BUFFER_DATA_LENGTH;
#endif
    MDL *MdlChain;
    ULONG DataOffset;
} NET_BUFFER_DATA, *PNET_BUFFER_DATA;

typedef union _NET_BUFFER_HEADER
{
#ifdef __cplusplus
    NET_BUFFER_DATA NetBufferData;
#else
    NET_BUFFER_DATA;
#endif
    SLIST_HEADER Link;

} NET_BUFFER_HEADER, *PNET_BUFFER_HEADER;

#endif // NDIS_INCLUDE_LEGACY_NAMES

#if NDIS_SUPPORT_NDIS620

typedef struct _NET_BUFFER_SHARED_MEMORY NET_BUFFER_SHARED_MEMORY;

//
// NET_BUFFER_SHARED_MEMORY is used to describe the
// shared memory segments used in each NET_BUFFER.
// for NDIS 6.20, they are used in VM queue capable NICs
// used in virtualization environment
//
typedef struct _NET_BUFFER_SHARED_MEMORY
{
    NET_BUFFER_SHARED_MEMORY   *NextSharedMemorySegment;
    ULONG                       SharedMemoryFlags;
    NDIS_HANDLE                 SharedMemoryHandle;
    ULONG                       SharedMemoryOffset;
    ULONG                       SharedMemoryLength;
} NET_BUFFER_SHARED_MEMORY, *PNET_BUFFER_SHARED_MEMORY;

#endif // NDIS_SUPPORT_NDIS620

//
// The NET_BUFFER structure
//

typedef struct _NET_BUFFER
{
    union
    {
        struct
        {
            NET_BUFFER *Next;
            MDL *CurrentMdl;
            ULONG CurrentMdlOffset;
            union
            {
                ULONG DataLength;
                SIZE_T stDataLength;
            };

            MDL *MdlChain;
            ULONG DataOffset;
        };

        SLIST_HEADER Link;

#ifdef NDIS_INCLUDE_LEGACY_NAMES
        // Duplicate of the above union, for source-compatibility
        NET_BUFFER_HEADER NetBufferHeader;
#endif // NDIS_INCLUDE_LEGACY_NAMES
    };

    USHORT ChecksumBias;
    USHORT Reserved;
    NDIS_HANDLE NdisPoolHandle;
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) PVOID NdisReserved[2];
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) PVOID ProtocolReserved[6];
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) PVOID MiniportReserved[4];
    PHYSICAL_ADDRESS DataPhysicalAddress;
#if NDIS_SUPPORT_NDIS620
    union
    {
        NET_BUFFER_SHARED_MEMORY *SharedMemoryInfo;
        SCATTER_GATHER_LIST *ScatterGatherList;
    };
#endif
} NET_BUFFER, *PNET_BUFFER;

//
// The NET_BUFFER_LIST structure and its member structures
//

#pragma warning(push)
#pragma warning(disable:4200)   // nonstandard extension used : zero-sized array in struct/union

typedef struct _NET_BUFFER_LIST_CONTEXT
{
    NET_BUFFER_LIST_CONTEXT *Next;
    USHORT Size;
    USHORT Offset;
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) UCHAR ContextData[];
} NET_BUFFER_LIST_CONTEXT, *PNET_BUFFER_LIST_CONTEXT;

#pragma warning(pop)

#ifdef NDIS_INCLUDE_LEGACY_NAMES

typedef struct _NET_BUFFER_LIST_DATA
{
    NET_BUFFER_LIST *Next;      // Next NetBufferList in the chain
    NET_BUFFER *FirstNetBuffer; // First NetBuffer on this NetBufferList
} NET_BUFFER_LIST_DATA, *PNET_BUFFER_LIST_DATA;

typedef union _NET_BUFFER_LIST_HEADER
{
#ifdef __cplusplus
    NET_BUFFER_LIST_DATA NetBufferListData;
#else
    NET_BUFFER_LIST_DATA;
#endif
    SLIST_HEADER Link; // used in SLIST of free NetBuffers in the block
} NET_BUFFER_LIST_HEADER, *PNET_BUFFER_LIST_HEADER;

#endif // NDIS_INCLUDE_LEGACY_NAMES

typedef struct _NET_BUFFER_LIST
{
    union
    {
        struct
        {
            NET_BUFFER_LIST *Next;      // Next NetBufferList in the chain
            NET_BUFFER *FirstNetBuffer; // First NetBuffer on this NetBufferList
        };

        SLIST_HEADER Link; // used in SLIST of free NetBuffers in the block

#ifdef NDIS_INCLUDE_LEGACY_NAMES
        // Duplicate of the above, for source-compatibility
        NET_BUFFER_LIST_HEADER NetBufferListHeader;
#endif // NDIS_INCLUDE_LEGACY_NAMES
    };

    NET_BUFFER_LIST_CONTEXT *Context;
    NET_BUFFER_LIST *ParentNetBufferList;
    NDIS_HANDLE NdisPoolHandle;
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) PVOID NdisReserved[2];
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) PVOID ProtocolReserved[4];
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) PVOID MiniportReserved[2];
    PVOID Scratch;
    NDIS_HANDLE SourceHandle;
    ULONG NblFlags; // public flags
    LONG ChildRefCount;
    ULONG Flags; // private flags used by NDIs, protocols, miniport, etc.

    union
    {
        NDIS_STATUS Status;
        ULONG NdisReserved2;
    };

    PVOID NetBufferListInfo[MaxNetBufferListInfo];
} NET_BUFFER_LIST, *PNET_BUFFER_LIST;

//
// The flags that can be set at NET_BUFFER_LIST::Flags are defined below.
//
// N.B.: On Vista and Win7, bits 0x3 were reserved by NDIS.  However, starting
//       with Win8, these bits are free for protocol use.  Protocols may only
//       use these two additional bits if they verify that NdisGetVersion()
//       returns a value greater than or equal to NDIS_RUNTIME_VERSION_630.
//
#if NDIS_SUPPORT_NDIS630
#define NBL_FLAGS_PROTOCOL_RESERVED             0xFFF00003
#define NBL_FLAGS_MINIPORT_RESERVED             0x0000F000
#define NBL_FLAGS_SCRATCH                       0x000F0000
#define NBL_FLAGS_NDIS_RESERVED                 0x00000FFC
#else // <NDIS_SUPPORT_NDIS630
#define NBL_FLAGS_PROTOCOL_RESERVED             0xFFF00000
#define NBL_FLAGS_MINIPORT_RESERVED             0x0000F000
#define NBL_FLAGS_SCRATCH                       0x000F0000
#define NBL_FLAGS_NDIS_RESERVED                 0x00000FFF
#endif // NDIS_SUPPORT_NDIS630

//
// The flags that can be set at NET_BUFFER_LIST::NblFlags are defined below.
//
#define NDIS_NBL_FLAGS_SEND_READ_ONLY           0x00000001
#define NDIS_NBL_FLAGS_RECV_READ_ONLY           0x00000002
// Bits 0x04 to 0x80 are reserved for use by NDIS.

#if NDIS_SUPPORT_NDIS61
#define NDIS_NBL_FLAGS_HD_SPLIT                 0x00000100 // Data and header are split
#define NDIS_NBL_FLAGS_IS_IPV4                  0x00000200 // Packet is an IPv4 packet
#define NDIS_NBL_FLAGS_IS_IPV6                  0x00000400 // Packet is an IPv6 packet
#define NDIS_NBL_FLAGS_IS_TCP                   0x00000800 // Packet is a TCP packet
#define NDIS_NBL_FLAGS_IS_UDP                   0x00001000 // Packet is a UDP packet
#define NDIS_NBL_FLAGS_SPLIT_AT_UPPER_LAYER_PROTOCOL_HEADER \
                                                0x00002000 // Packet is split at the beginning of upper layer protocol header
#define NDIS_NBL_FLAGS_SPLIT_AT_UPPER_LAYER_PROTOCOL_PAYLOAD \
                                                0x00004000 // Packet is split at the beginning of upper layer protocol data (TCP or UDP)
#endif // NDIS_SUPPORT_NDIS61

#define NDIS_NBL_FLAGS_IS_LOOPBACK_PACKET       0x00008000 // The NBL is a layer-2 loopback NBL

#if NDIS_SUPPORT_NDIS682
#define NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT \
                                                0x00010000 // A packet timestamp requested
#endif // NDIS_SUPPORT_NDIS682


EXTERN_C_END

#pragma warning(pop)

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion


