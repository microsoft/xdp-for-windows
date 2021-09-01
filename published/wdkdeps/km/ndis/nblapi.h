// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/version.h>
#include <ndis/types.h>
#include <ndis/objectheader.h>
#include <ndis/nbl.h>

EXTERN_C_START

//
// NBL pool routines
//

#define NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1 1

typedef struct _NET_BUFFER_LIST_POOL_PARAMETERS
{
    /*
        Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        Parameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        Parameters.Header.Size = sizeof(Parameters);
    */
    NDIS_OBJECT_HEADER      Header;
    UCHAR                   ProtocolId;
    BOOLEAN                 fAllocateNetBuffer;
    USHORT                  ContextSize;
    ULONG                   PoolTag;
    ULONG                   DataSize;
} NET_BUFFER_LIST_POOL_PARAMETERS, *PNET_BUFFER_LIST_POOL_PARAMETERS;

#define NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NET_BUFFER_LIST_POOL_PARAMETERS, DataSize)

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_HANDLE
NdisAllocateNetBufferListPool(
    _In_opt_ NDIS_HANDLE                            NdisHandle,
    _In_ NET_BUFFER_LIST_POOL_PARAMETERS const     *Parameters
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeNetBufferListPool(
    _In_ __drv_freesMem(mem) NDIS_HANDLE            PoolHandle
    );

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NET_BUFFER_LIST *
NdisAllocateNetBufferList(
    _In_ NDIS_HANDLE                                PoolHandle,
    _In_ USHORT                                     ContextSize,
    _In_ USHORT                                     ContextBackFill
    );

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NET_BUFFER_LIST *
NdisAllocateNetBufferAndNetBufferList(
    _In_ NDIS_HANDLE                                PoolHandle,
    _In_ USHORT                                     ContextSize,
    _In_ USHORT                                     ContextBackFill,
    _In_opt_ __drv_aliasesMem MDL                  *MdlChain,
    _In_ ULONG                                      DataOffset,
    _In_ SIZE_T                                     DataLength
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeNetBufferList(
    _In_ __drv_freesMem(mem) NET_BUFFER_LIST       *NetBufferList
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_HANDLE
NdisGetPoolFromNetBufferList(
    _In_ NET_BUFFER_LIST const                     *NetBufferList
    );

//
// NB pool routines
//

#define NET_BUFFER_POOL_PARAMETERS_REVISION_1 1

typedef struct _NET_BUFFER_POOL_PARAMETERS
{
    /*
        Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        Parameters.Header.Revision = NET_BUFFER_POOL_PARAMETERS_REVISION_1;
        Parameters.Header.Size = sizeof(Parameters);
    */
    NDIS_OBJECT_HEADER      Header;
    ULONG                   PoolTag;
    ULONG                   DataSize;
} NET_BUFFER_POOL_PARAMETERS, *PNET_BUFFER_POOL_PARAMETERS;

#define NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_1   \
        RTL_SIZEOF_THROUGH_FIELD(NET_BUFFER_POOL_PARAMETERS, DataSize)

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_HANDLE
NdisAllocateNetBufferPool(
    _In_opt_ NDIS_HANDLE                        NdisHandle,
    _In_ NET_BUFFER_POOL_PARAMETERS const      *Parameters
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeNetBufferPool(
    _In_ __drv_freesMem(mem) NDIS_HANDLE        PoolHandle
    );

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NET_BUFFER *
NdisAllocateNetBuffer(
    _In_ NDIS_HANDLE                            PoolHandle,
    _In_opt_ MDL                               *MdlChain,
    _In_ ULONG                                  DataOffset,
    _In_ SIZE_T                                 DataLength
    );

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NET_BUFFER *
NdisAllocateNetBufferMdlAndData(
    _In_ NDIS_HANDLE                            PoolHandle
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeNetBuffer(
    _In_ __drv_freesMem(mem) NET_BUFFER        *NetBuffer
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_HANDLE
NdisGetPoolFromNetBuffer(
    _In_ NET_BUFFER const                      *NetBuffer
    );

//
// MDL allocation and free callbacks
//

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(NET_BUFFER_ALLOCATE_MDL)
MDL *
NET_BUFFER_ALLOCATE_MDL(
    _Inout_ ULONG                              *BufferSize
    );

typedef NET_BUFFER_ALLOCATE_MDL *NET_BUFFER_ALLOCATE_MDL_HANDLER;

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(NET_BUFFER_FREE_MDL)
VOID
NET_BUFFER_FREE_MDL(
    _In_ MDL                                   *Mdl
    );

typedef NET_BUFFER_FREE_MDL *NET_BUFFER_FREE_MDL_HANDLER;

//
// Advance/retreat routines
//

_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_STATUS
NdisRetreatNetBufferDataStart(
    _In_ NET_BUFFER                            *NetBuffer,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ ULONG                                  DataBackFill,
    _In_opt_ NET_BUFFER_ALLOCATE_MDL           *AllocateMdlHandler
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisAdvanceNetBufferDataStart(
    _In_ NET_BUFFER                            *NetBuffer,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ BOOLEAN                                FreeMdl,
    _In_opt_ NET_BUFFER_FREE_MDL               *FreeMdlHandler
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_STATUS
NdisRetreatNetBufferListDataStart(
    _In_ NET_BUFFER_LIST                       *NetBufferList,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ ULONG                                  DataBackFill,
    _In_opt_ NET_BUFFER_ALLOCATE_MDL           *AllocateMdlHandler,
    _In_opt_ NET_BUFFER_FREE_MDL               *FreeMdlHandler
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisAdvanceNetBufferListDataStart(
    _In_ NET_BUFFER_LIST                       *NetBufferList,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ BOOLEAN                                FreeMdl,
    _In_opt_ NET_BUFFER_FREE_MDL               *FreeMdlMdlHandler
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisAdjustNetBufferCurrentMdl(
    _In_ NET_BUFFER                            *NetBuffer
    );

//
// NBL context management routines
//

_IRQL_requires_max_(DISPATCH_LEVEL)
_When_(return==0,_At_(NetBufferList->Context, __drv_allocatesMem(mem)))
NDIS_EXPORTED_ROUTINE
NDIS_STATUS
NdisAllocateNetBufferListContext(
    _In_ NET_BUFFER_LIST                       *NetBufferList,
    _In_ USHORT                                 ContextSize,
    _In_ USHORT                                 ContextBackFill,
    _In_ ULONG                                  PoolTag
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
_At_(NetBufferList->Context, __drv_freesMem(mem))
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeNetBufferListContext(
    _In_ NET_BUFFER_LIST                       *NetBufferList,
    _In_ USHORT                                 ContextSize
    );

//
// NBL clone routines
//

#define NDIS_CLONE_FLAGS_RESERVED               0x00000001
#define NDIS_CLONE_FLAGS_USE_ORIGINAL_MDLS      0x00000002

#ifdef NDIS_INCLUDE_LEGACY_NAMES
#define NDIS_TEST_CLONE_FLAG(_Flags, _Fl)       (((_Flags) & (_Fl)) == (_Fl))
#define NDIS_SET_CLONE_FLAG(_Flags, _Fl)        ((_Flags) |= (_Fl))
#define NDIS_CLEAR_CLONE_FLAG(_Flags, _Fl)      ((_Flags) &= ~(_Fl))
#endif // NDIS_INCLUDE_LEGACY_NAMES

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NET_BUFFER_LIST *
NdisAllocateCloneNetBufferList(
    _In_ NET_BUFFER_LIST                       *OriginalNetBufferList,
    _In_opt_ NDIS_HANDLE                        NetBufferListPoolHandle,
    _In_opt_ NDIS_HANDLE                        NetBufferPoolHandle,
    _In_ ULONG                                  AllocateCloneFlags
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeCloneNetBufferList(
    _In_ __drv_freesMem(mem) NET_BUFFER_LIST   *CloneNetBufferList,
    _In_ ULONG                                  FreeCloneFlags
    );

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NET_BUFFER_LIST *
NdisAllocateFragmentNetBufferList(
    _In_ NET_BUFFER_LIST                       *OriginalNetBufferList,
    _In_opt_ NDIS_HANDLE                        NetBufferListPool,
    _In_opt_ NDIS_HANDLE                        NetBufferPool,
    _In_ ULONG                                  StartOffset,
    _In_ ULONG                                  MaximumLength,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ ULONG                                  DataBackFill,
    _In_ ULONG                                  AllocateFragmentFlags
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeFragmentNetBufferList(
    _In_ __drv_freesMem(mem) NET_BUFFER_LIST   *FragmentNetBufferList,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ ULONG                                  FreeFragmentFlags
    );

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NET_BUFFER_LIST *
NdisAllocateReassembledNetBufferList(
    _In_ NET_BUFFER_LIST                       *FragmentNetBufferList,
    _In_opt_ NDIS_HANDLE                        NetBufferAndNetBufferListPoolHandle,
    _In_ ULONG                                  StartOffset,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ ULONG                                  DataBackFill,
    _In_ ULONG                                  AllocateReassembleFlags
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeReassembledNetBufferList(
    _In_ __drv_freesMem(mem) NET_BUFFER_LIST   *ReassembledNetBufferList,
    _In_ ULONG                                  DataOffsetDelta,
    _In_ ULONG                                  FreeReassembleFlags
    );

//
// NBL metadata routines
//

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
UCHAR
NdisGetNetBufferListProtocolId(
    _In_ NET_BUFFER_LIST const                 *NetBufferList
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisCopySendNetBufferListInfo(
    _In_ NET_BUFFER_LIST                       *DestNetBufferList,
    _In_ NET_BUFFER_LIST const                 *SrcNetBufferList
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisCopyReceiveNetBufferListInfo(
    _In_ NET_BUFFER_LIST                       *DestNetBufferList,
    _In_ NET_BUFFER_LIST const                 *SrcNetBufferList
    );

//
// NB payload routines
//

_IRQL_requires_max_(DISPATCH_LEVEL)
_At_(AlignOffset, _In_range_(0, AlignMultiple-1))
_Pre_satisfies_(AlignMultiple == 1 || AlignMultiple == 2 || AlignMultiple == 4
        || AlignMultiple == 8 || AlignMultiple == 16 || AlignMultiple == 32
        || AlignMultiple == 64 || AlignMultiple ==128 || AlignMultiple ==256
        || AlignMultiple ==512 || AlignMultiple == 1024 || AlignMultiple == 2048
        || AlignMultiple == 4096 || AlignMultiple == 8192)
_Must_inspect_result_
NDIS_EXPORTED_ROUTINE
PVOID
NdisGetDataBuffer(
    _In_ NET_BUFFER                            *NetBuffer,
    _In_ ULONG                                  BytesNeeded,
    _Out_writes_bytes_all_opt_(BytesNeeded)
            PVOID                               Storage,
    _In_ ULONG                                  AlignMultiple,
    _In_ ULONG                                  AlignOffset
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
NDIS_STATUS
NdisCopyFromNetBufferToNetBuffer(
    _In_ NET_BUFFER                            *Destination,
    _In_ ULONG                                  DestinationOffset,
    _In_ ULONG                                  BytesToCopy,
    _In_ NET_BUFFER const                      *Source,
    _In_ ULONG                                  SourceOffset,
    _Out_ ULONG                                *BytesCopied
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
ULONG
NdisQueryNetBufferPhysicalCount(
    _In_ NET_BUFFER                            *NetBuffer
    );

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

