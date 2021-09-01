//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ntdef.h>
#include <ntstatus.h>
#include <ntifs.h>
#include <ntintsafe.h>
#include <ndis.h>
#include <stdlib.h>

#define XDPAPI
#define XDPEXPORT(RoutineName) RoutineName##Thunk

#define RTL_IS_POWER_OF_TWO(Value) \
    ((Value != 0) && !((Value) & ((Value) - 1)))

#define RTL_NUM_ALIGN_DOWN(Number, Alignment) \
    ((Number) - ((Number) & ((Alignment) - 1)))
#define RTL_NUM_ALIGN_UP(Number, Alignment) \
    RTL_NUM_ALIGN_DOWN((Number) + (Alignment) - 1, (Alignment))

#ifndef NTDDI_WIN10_CO // Temporary until the latest WDK is published.
#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union
typedef enum _DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_TYPE {
    CommonBufferConfigTypeLogicalAddressLimits,
    CommonBufferConfigTypeSubSection,
    CommonBufferConfigTypeHardwareAccessPermissions,
    CommonBufferConfigTypeMax,
} DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_TYPE,
* PDMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_TYPE;
typedef enum _DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_ACCESS_TYPE {
    CommonBufferHardwareAccessReadOnly,
    CommonBufferHardwareAccessWriteOnly,
    CommonBufferHardwareAccessReadWrite,
    CommonBufferHardwareAccessMax,
} DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_ACCESS_TYPE,
* PDMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_ACCESS_TYPE;
typedef struct _DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION {
    DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_TYPE ConfigType;
    union {
        struct {
            PHYSICAL_ADDRESS MinimumAddress;
            PHYSICAL_ADDRESS MaximumAddress;
        } LogicalAddressLimits;
        struct {
            ULONGLONG Offset;
            ULONG Length;
        } SubSection;
        DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION_ACCESS_TYPE HardwareAccessType;
        ULONGLONG Reserved[4];
    };
} DMA_COMMON_BUFFER_EXTENDED_CONFIGURATION,
* PDMA_COMMON_BUFFER_EXTENDED_CONFIGURATION;
typedef NTSTATUS
(*PCREATE_COMMON_BUFFER_FROM_MDL)(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_reads_opt_(ExtendedConfigsCount) PDMA_COMMON_BUFFER_EXTENDED_CONFIGURATION ExtendedConfigs,
    _In_ ULONG ExtendedConfigsCount,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress
    );
typedef struct _DMA_OPERATIONS_XDP {
    ULONG Size;
    PPUT_DMA_ADAPTER PutDmaAdapter;
    PALLOCATE_COMMON_BUFFER AllocateCommonBuffer;
    PFREE_COMMON_BUFFER FreeCommonBuffer;
    PALLOCATE_ADAPTER_CHANNEL AllocateAdapterChannel;
    PFLUSH_ADAPTER_BUFFERS FlushAdapterBuffers;
    PFREE_ADAPTER_CHANNEL FreeAdapterChannel;
    PFREE_MAP_REGISTERS FreeMapRegisters;
    PMAP_TRANSFER MapTransfer;
    PGET_DMA_ALIGNMENT GetDmaAlignment;
    PREAD_DMA_COUNTER ReadDmaCounter;
    PGET_SCATTER_GATHER_LIST GetScatterGatherList;
    PPUT_SCATTER_GATHER_LIST PutScatterGatherList;
    PCALCULATE_SCATTER_GATHER_LIST_SIZE CalculateScatterGatherList;
    PBUILD_SCATTER_GATHER_LIST BuildScatterGatherList;
    PBUILD_MDL_FROM_SCATTER_GATHER_LIST BuildMdlFromScatterGatherList;
    PGET_DMA_ADAPTER_INFO GetDmaAdapterInfo;
    PGET_DMA_TRANSFER_INFO GetDmaTransferInfo;
    PINITIALIZE_DMA_TRANSFER_CONTEXT InitializeDmaTransferContext;
    PALLOCATE_COMMON_BUFFER_EX AllocateCommonBufferEx;
    PALLOCATE_ADAPTER_CHANNEL_EX AllocateAdapterChannelEx;
    PCONFIGURE_ADAPTER_CHANNEL ConfigureAdapterChannel;
    PCANCEL_ADAPTER_CHANNEL CancelAdapterChannel;
    PMAP_TRANSFER_EX MapTransferEx;
    PGET_SCATTER_GATHER_LIST_EX GetScatterGatherListEx;
    PBUILD_SCATTER_GATHER_LIST_EX BuildScatterGatherListEx;
    PFLUSH_ADAPTER_BUFFERS_EX FlushAdapterBuffersEx;
    PFREE_ADAPTER_OBJECT FreeAdapterObject;
    PCANCEL_MAPPED_TRANSFER CancelMappedTransfer;
    PALLOCATE_DOMAIN_COMMON_BUFFER AllocateDomainCommonBuffer;
    PFLUSH_DMA_BUFFER FlushDmaBuffer;
    PJOIN_DMA_DOMAIN JoinDmaDomain;
    PLEAVE_DMA_DOMAIN LeaveDmaDomain;
    PGET_DMA_DOMAIN GetDmaDomain;
    PALLOCATE_COMMON_BUFFER_WITH_BOUNDS AllocateCommonBufferWithBounds;
    PALLOCATE_COMMON_BUFFER_VECTOR AllocateCommonBufferVector;
    PGET_COMMON_BUFFER_FROM_VECTOR_BY_INDEX GetCommonBufferFromVectorByIndex;
    PFREE_COMMON_BUFFER_FROM_VECTOR FreeCommonBufferFromVector;
    PFREE_COMMON_BUFFER_VECTOR FreeCommonBufferVector;
    PCREATE_COMMON_BUFFER_FROM_MDL CreateCommonBufferFromMdl;
} DMA_OPERATIONS_XDP;
#define DMA_OPERATIONS DMA_OPERATIONS_XDP
#endif // NTDDI_WIN10_CO

#include <xdp/bufferinterfacecontext.h>
#include <xdp/bufferlogicaladdress.h>
#include <xdp/buffermdl.h>
#include <xdp/buffervirtualaddress.h>
#include <xdp/control.h>
#include <xdp/datapath.h>
#include <xdp/framefragment.h>
#include <xdp/frameinterfacecontext.h>
#include <xdp/framerxaction.h>
#include <xdp/frametxcompletioncontext.h>

#include <msxdp.h>
#include <xdpassert.h>
#include <xdpif.h>
#include <xdpioctl.h>
#include <xdplwf.h>
#include <xdpnmrprovider.h>
#include <xdppollshim.h>
#include <xdprefcount.h>
#include <xdpregistry.h>
#include <xdprtl.h>
#include <xdprxqueue_internal.h>
#include <xdptrace.h>
#include <xdptxqueue_internal.h>
#include <xdpworkqueue.h>

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#include "xdpp.h"
#include "bind.h"
#include "dispatch.h"
#include "extensionset.h"
#include "program.h"
#include "queue.h"
#include "redirect.h"
#include "ring.h"
#include "rx.h"
#include "tx.h"
#include "xsk.h"
