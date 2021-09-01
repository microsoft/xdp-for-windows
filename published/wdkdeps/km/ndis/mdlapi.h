// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

EXTERN_C_START

#include <ndis/types.h>

typedef struct _MDL MDL;

#ifdef NDIS_INCLUDE_LEGACY_NAMES

#define NdisQueryMdl(_Mdl, _VirtualAddress, _Length, _Priority)             \
{                                                                           \
    if (ARGUMENT_PRESENT(_VirtualAddress))                                  \
    {                                                                       \
        *(PVOID *)(_VirtualAddress) = MmGetSystemAddressForMdlSafe(_Mdl, ( _Priority | MdlMappingNoExecute )); \
    }                                                                       \
    *(_Length) = MmGetMdlByteCount(_Mdl);                                   \
}

#define NdisQueryMdlOffset(_Mdl, _Offset, _Length)                          \
{                                                                           \
    *(_Offset) = MmGetMdlByteOffset(_Mdl);                                  \
    *(_Length) = MmGetMdlByteCount(_Mdl);                                   \
}

#define NDIS_MDL_TO_SPAN_PAGES(_Mdl)                                        \
    (MmGetMdlByteCount(_Mdl)==0 ?                                           \
                1 :                                                         \
                (ADDRESS_AND_SIZE_TO_SPAN_PAGES(                            \
                        MmGetMdlVirtualAddress(_Mdl),                       \
                        MmGetMdlByteCount(_Mdl))))

#define NdisGetMdlPhysicalArraySize(_Mdl, _ArraySize)                       \
    (*(_ArraySize) = NDIS_MDL_TO_SPAN_PAGES(_Mdl))


#define NDIS_MDL_LINKAGE(_Mdl) ((_Mdl)->Next)

#define NdisGetNextMdl(_CurrentMdl, _NextMdl)                               \
{                                                                           \
    *(_NextMdl) = (_CurrentMdl)->Next;                                      \
}

#else // NDIS_INCLUDE_LEGACY_NAMES

inline
void
NdisQueryMdl(
    _In_ MDL                                   *Mdl,
    _Out_opt_ PVOID                            *VirtualAddress,
    _Out_ ULONG                                 Length,
    _In_ MM_PAGE_PRIORITY                       Priority
    )
{
    if (ARGUMENT_PRESENT(VirtualAddress))
    {
        *VirtualAddress = MmGetSystemAddressForMdlSafe(Mdl, Priority | MdlMappingNoExecute);
    }

    *Length = MmGetMdlByteCount(Mdl);
}

#endif // NDIS_INCLUDE_LEGACY_NAMES

_Must_inspect_result_
__drv_allocatesMem(mem)
_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
MDL *
NdisAllocateMdl(
    _In_ NDIS_HANDLE                            NdisHandle,
    _In_reads_bytes_(Length) PVOID              VirtualAddress,
    _In_ ULONG                                  Length
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_EXPORTED_ROUTINE
VOID
NdisFreeMdl(
    _In_ __drv_freesMem(mem) MDL               *Mdl
    );

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

