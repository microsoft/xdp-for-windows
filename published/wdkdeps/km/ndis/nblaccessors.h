// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/version.h>
#include <ndis/nbl.h>
#include <ndis/nblinfo.h>

EXTERN_C_START

//
// NET_BUFFER field accessors
//

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NET_BUFFER_NEXT_NB(_NB)                     ((_NB)->Next)
#define NET_BUFFER_FIRST_MDL(_NB)                   ((_NB)->MdlChain)
#define NET_BUFFER_DATA_LENGTH(_NB)                 ((_NB)->DataLength)
#define NET_BUFFER_DATA_OFFSET(_NB)                 ((_NB)->DataOffset)
#define NET_BUFFER_CURRENT_MDL(_NB)                 ((_NB)->CurrentMdl)
#define NET_BUFFER_CURRENT_MDL_OFFSET(_NB)          ((_NB)->CurrentMdlOffset)

#define NET_BUFFER_PROTOCOL_RESERVED(_NB)           ((_NB)->ProtocolReserved)
#define NET_BUFFER_MINIPORT_RESERVED(_NB)           ((_NB)->MiniportReserved)
#define NET_BUFFER_CHECKSUM_BIAS(_NB)               ((_NB)->ChecksumBias)

#if (NDIS_SUPPORT_NDIS61)
#define NET_BUFFER_DATA_PHYSICAL_ADDRESS(_NB)       ((_NB)->DataPhysicalAddress)
#endif // (NDIS_SUPPORT_NDIS61)

#if (NDIS_SUPPORT_NDIS620)
#define NET_BUFFER_FIRST_SHARED_MEM_INFO(_NB)       ((_NB)->SharedMemoryInfo)
#define NET_BUFFER_SHARED_MEM_NEXT_SEGMENT(_SHI)    ((_SHI)->NextSharedMemorySegment)
#define NET_BUFFER_SHARED_MEM_FLAGS(_SHI)           ((_SHI)->SharedMemoryFlags)
#define NET_BUFFER_SHARED_MEM_HANDLE(_SHI)          ((_SHI)->SharedMemoryHandle)
#define NET_BUFFER_SHARED_MEM_OFFSET(_SHI)          ((_SHI)->SharedMemoryOffset)
#define NET_BUFFER_SHARED_MEM_LENGTH(_SHI)          ((_SHI)->SharedMemoryLength)

#define NET_BUFFER_SCATTER_GATHER_LIST(_NB)         ((_NB)->ScatterGatherList)

#endif // (NDIS_SUPPORT_NDIS620)

#endif // NDIS_INCLUDE_LEGACY_NAMES

#if defined(NDIS_INCLUDE_LEGACY_NAMES) || !defined(__cplusplus)
#define NET_BUFFER_LIST_INFO(_NBL, _Id)             ((_NBL)->NetBufferListInfo[(_Id)])
#else
extern "C++"
inline
void *&
NET_BUFFER_LIST_INFO(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ NDIS_NET_BUFFER_LIST_INFO              Id
    )
{
    return Nbl->NetBufferListInfo[Id];
}

extern "C++"
inline
void * const &
NET_BUFFER_LIST_INFO(
    _In_ NET_BUFFER_LIST                 const *Nbl,
    _In_ NDIS_NET_BUFFER_LIST_INFO              Id
    )
{
    return Nbl->NetBufferListInfo[Id];
}
#endif // NDIS_INCLUDE_LEGACY_NAMES || !__cplusplus

#ifdef NDIS_INCLUDE_LEGACY_NAMES
#define NBL_TEST_FLAG(_NBL, _F)                 (((_NBL)->Flags & (_F)) != 0)
#define NBL_SET_FLAG(_NBL, _F)                  ((_NBL)->Flags |= (_F))
#define NBL_CLEAR_FLAG(_NBL, _F)                ((_NBL)->Flags &= ~(_F))
#else // NDIS_INCLUDE_LEGACY_NAMES
inline
BOOLEAN
NBL_TEST_FLAG(
    _In_ NET_BUFFER_LIST const                 *Nbl,
    _In_ ULONG                                  Flag
    )
{
    return 0 != (Nbl->Flags & Flag);
}

inline
BOOLEAN
NBL_TEST_FLAGS(
    _In_ NET_BUFFER_LIST const                 *Nbl,
    _In_ ULONG                                  Flags
    )
{
    return Flags == (Nbl->Flags & Flags);
}

inline
void
NBL_SET_FLAG(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ ULONG                                  Flag
    )
{
    Nbl->Flags |= Flag;
}

inline
void
NBL_CLEAR_FLAG(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ ULONG                                  Flag
    )
{
    Nbl->Flags &= ~Flag;
}
#endif // NDIS_INCLUDE_LEGACY_NAMES

#ifdef NDIS_INCLUDE_LEGACY_NAMES
#define NBL_SET_PROTOCOL_RSVD_FLAG(_NBL, _F)    ((_NBL)->Flags |= ((_F) & NBL_FLAGS_PROTOCOL_RESERVED))
#define NBL_CLEAR_PROTOCOL_RSVD_FLAG(_NBL, _F)  ((_NBL)->Flags &= ~((_F) & NBL_FLAGS_PROTOCOL_RESERVED))
#define NBL_TEST_PROTOCOL_RSVD_FLAG(_NBL, _F)   (((_NBL)->Flags & ((_F) & NBL_FLAGS_PROTOCOL_RESERVED)) != 0)

#define NBL_PROT_RSVD_FLAGS                     NBL_FLAGS_PROTOCOL_RESERVED
#define NBL_SET_PROT_RSVD_FLAG(_NBL, _F)        NBL_SET_PROTOCOL_RSVD_FLAG(_NBL,_F)
#define NBL_CLEAR_PROT_RSVD_FLAG(_NBL, _F)      NBL_CLEAR_PROTOCOL_RSVD_FLAG(_NBL, _F)
#define NBL_TEST_PROT_RSVD_FLAG(_NBL, _F)       NBL_TEST_PROTOCOL_RSVD_FLAG(_NBL, _F)
#endif // NDIS_INCLUDE_LEGACY_NAMES

#ifdef NDIS_INCLUDE_LEGACY_NAMES
#define NdisTestNblFlag(_NBL, _F)               (((_NBL)->NblFlags & (_F)) != 0)
#define NdisTestNblFlags(_NBL, _F)              (((_NBL)->NblFlags & (_F)) == (_F))
#define NdisSetNblFlag(_NBL, _F)                ((_NBL)->NblFlags |= (_F))
#define NdisClearNblFlag(_NBL, _F)              ((_NBL)->NblFlags &= ~(_F))
#else // NDIS_INCLUDE_LEGACY_NAMES
inline
BOOLEAN
NdisTestNblFlag(
    _In_ NET_BUFFER_LIST const                 *Nbl,
    _In_ ULONG                                  Flag
    )
{
    return 0 != (Nbl->NblFlags & Flag);
}

inline
BOOLEAN
NdisTestNblFlags(
    _In_ NET_BUFFER_LIST const                 *Nbl,
    _In_ ULONG                                  Flags
    )
{
    return Flags == (Nbl->NblFlags & Flags);
}

inline
void
NdisSetNblFlag(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ ULONG                                  Flag
    )
{
    Nbl->NblFlags |= Flag;
}

inline
void
NdisClearNblFlag(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ ULONG                                  Flag
    )
{
    Nbl->NblFlags &= ~Flag;
}
#endif // NDIS_INCLUDE_LEGACY_NAMES

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NET_BUFFER_LIST_NEXT_NBL(_NBL)              ((_NBL)->Next)
#define NET_BUFFER_LIST_FIRST_NB(_NBL)              ((_NBL)->FirstNetBuffer)

#define NET_BUFFER_LIST_FLAGS(_NBL)                 ((_NBL)->Flags)
#define NET_BUFFER_LIST_NBL_FLAGS(_NBL)             ((_NBL)->NblFlags)
#define NET_BUFFER_LIST_PROTOCOL_RESERVED(_NBL)     ((_NBL)->ProtocolReserved)
#define NET_BUFFER_LIST_MINIPORT_RESERVED(_NBL)     ((_NBL)->MiniportReserved)

#define NET_BUFFER_LIST_STATUS(_NBL)                ((_NBL)->Status)

#define NdisSetNetBufferListProtocolId(_NBL,_ProtocolId)   \
    *((PUCHAR)(&NET_BUFFER_LIST_INFO(_NBL, NetBufferListProtocolId))) = _ProtocolId

#else // NDIS_INCLUDE_LEGACY_NAMES

inline
void
NdisSetNetBufferListProtocolId(
    _In_ NET_BUFFER_LIST                       *Nbl,
    _In_ UCHAR                                  ProtocolId
    )
{
    *((UCHAR*)(&NET_BUFFER_LIST_INFO(Nbl, NetBufferListProtocolId))) = ProtocolId;
}

#endif // NDIS_INCLUDE_LEGACY_NAMES

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

