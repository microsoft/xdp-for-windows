//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements the BPF_MAP_TYPE_XSKMAP extensible map provider,
// enabling eBPF programs to redirect packets to AF_XDP sockets via
// bpf_redirect_map().
//
// The XSKMAP relies entirely on the eBPF base hash map for storage. The provider
// callbacks intercept CRUD operations to reference-count XSK handles. The base map
// stores HANDLE-sized values; the provider validates and transforms them on add/delete.
//

#include "precomp.h"
#include "ebpfxskmap.h"
#include "ebpfxskmap.tmh"

//
// Per-binding context for the XSKMAP provider. Created when the eBPF runtime
// attaches as a client; stores a copy of the client's dispatch table.
//
typedef struct _XDP_XSKMAP_BINDING_CONTEXT {
    ebpf_base_map_client_dispatch_table_t ClientDispatch;
} XDP_XSKMAP_BINDING_CONTEXT;

//
// Per-map context for an XSKMAP instance.
//
typedef struct _XDP_XSKMAP_CONTEXT {
    XDP_EBPF_MAP_HEADER Header;
    ebpf_base_map_client_dispatch_table_t *ClientDispatch;
} XDP_XSKMAP_CONTEXT;

//
// XSKMAP provider module ID.
// {1b8c9ddd-9722-467c-b2b0-4ffa7fb7fe84}
//
static const NPI_MODULEID EbpfXskmapProviderModuleId = {
    .Length = sizeof(NPI_MODULEID),
    .Type = MIT_GUID,
    .Guid = {
        0x1b8c9ddd,
        0x9722,
        0x467c,
        {0xb2, 0xb0, 0x4f, 0xfa, 0x7f, 0xb7, 0xfe, 0x84}
    },
};

static EBPF_EXTENSION_PROVIDER *EbpfXskmapProvider;

//
// Offset within the eBPF map structure where the provider context (map_context)
// is stored. Set during client attach; used by XdpXskmapFindElement to resolve
// a raw map pointer to its XDP_EBPF_MAP_HEADER.
//
// N.B. The eBPF contract guarantees all maps share the same context offset.
//
static ULONG64 XdpXskmapContextOffset;

static
XDP_XSKMAP_BINDING_CONTEXT *
XdpXskmapGetBindingContext(
    _In_ void *BindingContext
    )
{
    return (XDP_XSKMAP_BINDING_CONTEXT *)EbpfExtensionClientGetProviderData(
        (const EBPF_EXTENSION_CLIENT *)BindingContext);
}

static
ebpf_result_t
XdpXskmapPreprocessMapCreate(
    _In_ void *BindingContext,
    uint32_t MapType,
    uint32_t KeySize,
    uint32_t ValueSize,
    uint32_t MaxEntries,
    _Out_ uint32_t *ActualValueSize,
    _Outptr_ void **MapContext
    )
{
    XDP_XSKMAP_CONTEXT *Context = NULL;
    XDP_XSKMAP_BINDING_CONTEXT *Binding = XdpXskmapGetBindingContext(BindingContext);
    ebpf_result_t Result;

    UNREFERENCED_PARAMETER(MaxEntries);

    TraceEnter(
        TRACE_CORE, "MapType=%u KeySize=%u ValueSize=%u MaxEntries=%u",
        MapType, KeySize, ValueSize, MaxEntries);

    *ActualValueSize = 0;
    *MapContext = NULL;

    if (MapType != BPF_MAP_TYPE_XSKMAP) {
        Result = EBPF_OPERATION_NOT_SUPPORTED;
        goto Exit;
    }

    if (KeySize != sizeof(UINT64) || ValueSize != sizeof(HANDLE)) {
        Result = EBPF_INVALID_ARGUMENT;
        goto Exit;
    }

    //
    // The value stored in the base map is a referenced XSK handle (pointer).
    //
    *ActualValueSize = sizeof(HANDLE);

    //
    // Allocate the per-map context using epoch-protected allocation.
    //
    Context = Binding->ClientDispatch.epoch_allocate_with_tag(sizeof(*Context), XDP_POOLTAG_MAP);
    if (Context == NULL) {
        Result = EBPF_NO_MEMORY;
        goto Exit;
    }

    Context->Header.Type = XdpEbpfMapTypeXsk;
    Context->ClientDispatch = &Binding->ClientDispatch;
    *MapContext = &Context->Header;
    Result = EBPF_SUCCESS;

Exit:

    if (Result != EBPF_SUCCESS) {
        EventWriteEbpfXskmapCreateFailure(
            &MICROSOFT_XDP_PROVIDER, BindingContext, MapType, KeySize, ValueSize, (UINT32)Result);
    }

    TraceExitEbpfResult(TRACE_CORE);
    return Result;
}

static
void
XdpXskmapPostprocessMapDelete(
    _In_ void *BindingContext,
    _In_ _Post_invalid_ void *MapContext
    )
{
    XDP_XSKMAP_CONTEXT *Context = CONTAINING_RECORD(MapContext, XDP_XSKMAP_CONTEXT, Header);

    UNREFERENCED_PARAMETER(BindingContext);

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    //
    // Note: The eBPF runtime calls process_map_delete_element for each entry
    // before calling process_map_delete, so all XSK handles are already
    // dereferenced by the time we get here.
    //

    Context->ClientDispatch->epoch_free(Context);

    TraceExitSuccess(TRACE_CORE);
}

static
ebpf_result_t
XdpXskmapPreprocessAssociateProgramType(
    _In_ void *BindingContext,
    _In_ void *MapContext,
    _In_ const ebpf_program_type_t *ProgramType
    )
{
    static const ebpf_program_type_t ExpectedProgramType = EBPF_PROGRAM_TYPE_XDP_INIT;
    ebpf_result_t Result;

    UNREFERENCED_PARAMETER(BindingContext);

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    if (!IsEqualGUID(ProgramType, &ExpectedProgramType)) {
        TraceError(TRACE_CORE, "XSKMAP only supports XDP program type");
        EventWriteEbpfXskmapAssociateFailure(&MICROSOFT_XDP_PROVIDER, MapContext);
        Result = EBPF_OPERATION_NOT_SUPPORTED;
        goto Exit;
    }

    Result = EBPF_SUCCESS;

Exit:

    TraceExitEbpfResult(TRACE_CORE);
    return Result;
}

static
ebpf_result_t
XdpXskmapPostprocessMapFindElement(
    _In_ void *BindingContext,
    _In_ void *MapContext,
    size_t KeySize,
    _In_reads_opt_(KeySize) const uint8_t *Key,
    size_t InValueSize,
    _In_reads_(InValueSize) const uint8_t *InValue,
    size_t OutValueSize,
    _Out_writes_opt_(OutValueSize) uint8_t *OutValue,
    uint32_t Flags
    )
{
    UNREFERENCED_PARAMETER(BindingContext);
    UNREFERENCED_PARAMETER(KeySize);
    UNREFERENCED_PARAMETER(Key);
    UNREFERENCED_PARAMETER(InValueSize);
    UNREFERENCED_PARAMETER(InValue);
    UNREFERENCED_PARAMETER(OutValueSize);
    UNREFERENCED_PARAMETER(OutValue);

    //
    // This provider sets updates_original_value, so the eBPF runtime blocks
    // find-element lookups issued by a kernel BPF program (the helper path,
    // flagged with EBPF_MAP_OPERATION_HELPER). This callback therefore only
    // runs for BPF user-mode API lookups; assert the helper flag is clear.
    //
    ASSERT(!(Flags & EBPF_MAP_OPERATION_HELPER));

    //
    // An XSKMAP value is a referenced kernel XSK handle (pointer). Returning it
    // to user mode would leak a kernel pointer, so reject all user-mode
    // lookups. This matches Linux behavior.
    // Lookups from kernel BPF programs (the useful case) are not yet
    // permitted by the eBPF runtime; see the tracking issues on the PR
    // (ebpf-for-windows#5464 and xdp-for-windows#1049).
    //
    EventWriteEbpfXskmapFindElementRejected(&MICROSOFT_XDP_PROVIDER, MapContext, Flags);
    return EBPF_OPERATION_NOT_SUPPORTED;
}

static
ebpf_result_t
XdpXskmapPreprocessMapUpdateElement(
    _In_ void *BindingContext,
    _In_ void *MapContext,
    size_t KeySize,
    _In_reads_opt_(KeySize) const uint8_t *Key,
    size_t InValueSize,
    _In_reads_(InValueSize) const uint8_t *InValue,
    size_t OutValueSize,
    _Out_writes_opt_(OutValueSize) uint8_t *OutValue,
    uint32_t Flags
    )
{
    HANDLE NewXskHandle = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(BindingContext);
    UNREFERENCED_PARAMETER(MapContext);
    UNREFERENCED_PARAMETER(KeySize);
    UNREFERENCED_PARAMETER(Key);
    DBG_UNREFERENCED_PARAMETER(Flags);

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    //
    // This provider sets updates_original_value, so the eBPF runtime blocks
    // element updates issued by a kernel BPF program (the helper path, flagged
    // with EBPF_MAP_OPERATION_HELPER); this callback runs for BPF user-mode API
    // updates. Assert the helper flag is clear.
    //
    ASSERT(!(Flags & EBPF_MAP_OPERATION_HELPER));

    if (InValueSize != sizeof(HANDLE) || InValue == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (OutValue == NULL || OutValueSize != sizeof(HANDLE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // Reference the XSK handle provided by the user. This validates the handle
    // and increments the reference count.
    //
    Status = XskReferenceDatapathHandle(UserMode, InValue, TRUE, &NewXskHandle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Write the referenced handle into the output value buffer. The base map
    // will store this as the entry's value.
    //
    *(HANDLE *)OutValue = NewXskHandle;
    EventWriteEbpfXskmapUpdateElement(&MICROSOFT_XDP_PROVIDER, MapContext, NewXskHandle);
    NewXskHandle = NULL;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        EventWriteEbpfXskmapUpdateElementFailure(&MICROSOFT_XDP_PROVIDER, MapContext, (UINT32)Status);
    }

    if (NewXskHandle != NULL) {
        XskDereferenceDatapathHandle(NewXskHandle);
    }

    TraceExitStatus(TRACE_CORE);

    return NT_SUCCESS(Status) ? EBPF_SUCCESS : EBPF_INVALID_ARGUMENT;
}

static
void
XdpXskmapPostprocessMapDeleteElement(
    _In_ void *BindingContext,
    _In_ void *MapContext,
    size_t KeySize,
    _In_reads_opt_(KeySize) const uint8_t *Key,
    size_t ValueSize,
    _In_reads_(ValueSize) const uint8_t *Value,
    uint32_t Flags
    )
{
    HANDLE XskHandle;

    UNREFERENCED_PARAMETER(BindingContext);
    UNREFERENCED_PARAMETER(MapContext);
    UNREFERENCED_PARAMETER(KeySize);
    UNREFERENCED_PARAMETER(Key);
    DBG_UNREFERENCED_PARAMETER(Flags);

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    //
    // This provider sets updates_original_value, so the eBPF runtime blocks
    // element deletes issued by a kernel BPF program (the helper path, flagged
    // with EBPF_MAP_OPERATION_HELPER); this callback runs for BPF user-mode API
    // deletes and during map cleanup. Assert the helper flag is clear.
    //
    ASSERT(!(Flags & EBPF_MAP_OPERATION_HELPER));

    //
    // The Value parameter contains the entry being removed from the base map.
    // Dereference the XSK handle stored there.
    //
    if (ValueSize == sizeof(HANDLE) && Value != NULL) {
        XskHandle = *(const HANDLE *)Value;
    } else {
        XskHandle = NULL;
    }

    if (XskHandle != NULL) {
        XskDereferenceDatapathHandle(XskHandle);
        EventWriteEbpfXskmapDeleteElement(&MICROSOFT_XDP_PROVIDER, MapContext, XskHandle);
    } else {
        //
        // The base map handed back an entry that is not a valid XSK handle
        // (unexpected value size or NULL handle). Log the anomaly.
        //
        EventWriteEbpfXskmapDeleteElementFailure(
            &MICROSOFT_XDP_PROVIDER, MapContext, (UINT32)ValueSize);
    }

    TraceExitSuccess(TRACE_CORE);
}

//
// Provider dispatch table for XSKMAP operations.
//
static const ebpf_base_map_provider_dispatch_table_t XdpXskmapProviderDispatchTable = {
    .header = EBPF_BASE_MAP_PROVIDER_DISPATCH_TABLE_HEADER,
    .preprocess_map_create = XdpXskmapPreprocessMapCreate,
    .postprocess_map_delete = XdpXskmapPostprocessMapDelete,
    .preprocess_associate_program_type = XdpXskmapPreprocessAssociateProgramType,
    .postprocess_map_find_element = XdpXskmapPostprocessMapFindElement,
    .preprocess_map_update_element = XdpXskmapPreprocessMapUpdateElement,
    .postprocess_map_delete_element = XdpXskmapPostprocessMapDeleteElement,
};

static const ebpf_base_map_provider_properties_t XdpXskmapProviderProperties = {
    .header = EBPF_BASE_MAP_PROVIDER_PROPERTIES_HEADER,
    .updates_original_value = TRUE,
};

static const ebpf_map_provider_data_t XdpXskmapProviderData = {
    .header = EBPF_MAP_PROVIDER_DATA_HEADER,
    .map_type = BPF_MAP_TYPE_XSKMAP,
    .base_map_type = BPF_MAP_TYPE_HASH,
    //
    // ebpf_map_provider_data_t stores non-const pointers, so cast away const.
    // The eBPF runtime treats the provider data as read-only.
    //
    .base_properties = (ebpf_base_map_provider_properties_t *)&XdpXskmapProviderProperties,
    .base_provider_table = (ebpf_base_map_provider_dispatch_table_t *)&XdpXskmapProviderDispatchTable,
};

//
// Client attach callback: create a per-binding context with a copy of the
// client's dispatch table.
//
static
NTSTATUS
XdpXskmapOnClientAttach(
    _In_ const EBPF_EXTENSION_CLIENT *AttachingClient,
    _In_ const EBPF_EXTENSION_PROVIDER *AttachingProvider
    )
{
    const ebpf_extension_data_t *ClientExtData;
    const ebpf_map_client_data_t *ClientData;
    XDP_XSKMAP_BINDING_CONTEXT *Binding;

    UNREFERENCED_PARAMETER(AttachingProvider);

    TraceEnter(TRACE_CORE, "Client=%p", AttachingClient);

    ClientExtData = EbpfExtensionClientGetClientData(AttachingClient);
    if (ClientExtData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    ClientData = (const ebpf_map_client_data_t *)ClientExtData;

    Binding = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Binding), XDP_POOLTAG_EBPF_NMR);
    if (Binding == NULL) {
        return STATUS_NO_MEMORY;
    }

    RtlCopyMemory(
        &Binding->ClientDispatch, ClientData->base_client_table,
        min(sizeof(Binding->ClientDispatch), ClientData->base_client_table->header.total_size));

    WriteULong64NoFence(
        &XdpXskmapContextOffset, ClientData->map_context_offset);

    EbpfExtensionClientSetProviderData(AttachingClient, Binding);

    TraceExitSuccess(TRACE_CORE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
XdpXskmapOnClientDetach(
    _In_ const EBPF_EXTENSION_CLIENT *DetachingClient
    )
{
    XDP_XSKMAP_BINDING_CONTEXT *Binding = EbpfExtensionClientGetProviderData(DetachingClient);

    TraceEnter(TRACE_CORE, "Client=%p", DetachingClient);

    if (Binding != NULL) {
        ExFreePoolWithTag(Binding, XDP_POOLTAG_EBPF_NMR);
    }

    TraceExitSuccess(TRACE_CORE);
    return STATUS_SUCCESS;
}

ebpf_result_t
XdpXskmapFindElement(
    _In_ const VOID *Map,
    _In_ const VOID *Key,
    _Outptr_ VOID **Value
    )
{
    XDP_EBPF_MAP_HEADER *Header;
    XDP_XSKMAP_CONTEXT *Context;

    *Value = NULL;

    //
    // Resolve the provider context stored at the shared map-context offset. A
    // NULL context indicates a map with no XDP map provider, e.g. a core eBPF
    // array/hash map passed to bpf_redirect_map.
    //
    Header = *(XDP_EBPF_MAP_HEADER **)MAP_CONTEXT(Map, ReadULong64NoFence(&XdpXskmapContextOffset));
    if (Header == NULL) {
        return EBPF_OPERATION_NOT_SUPPORTED;
    }

    if (Header->Type != XdpEbpfMapTypeXsk) {
        return EBPF_OPERATION_NOT_SUPPORTED;
    }

    Context = CONTAINING_RECORD(Header, XDP_XSKMAP_CONTEXT, Header);

    return Context->ClientDispatch->find_element_function(Map, Key, (uint8_t **)Value);
}

NTSTATUS
XdpXskmapStart(
    VOID
    )
{
    const EBPF_EXTENSION_PROVIDER_PARAMETERS Parameters = {
        .ProviderModuleId = &EbpfXskmapProviderModuleId,
        .ProviderData = &XdpXskmapProviderData,
    };
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "-");

    Status =
        EbpfExtensionProviderRegister(
            &EBPF_MAP_INFO_EXTENSION_IID, &Parameters,
            XdpXskmapOnClientAttach, XdpXskmapOnClientDetach,
            NULL, &EbpfXskmapProvider);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CORE, "Failed to register XSKMAP provider Status=%!STATUS!", Status);
        goto Exit;
    }

Exit:

    TraceExitStatus(TRACE_CORE);
    return Status;
}

VOID
XdpXskmapStop(
    VOID
    )
{
    TraceEnter(TRACE_CORE, "-");

    if (EbpfXskmapProvider != NULL) {
        EbpfExtensionProviderUnregister(EbpfXskmapProvider);
        EbpfXskmapProvider = NULL;
    }

    TraceExitSuccess(TRACE_CORE);
}
