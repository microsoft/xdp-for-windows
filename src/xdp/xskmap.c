//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module implements the BPF_MAP_TYPE_XSKMAP extensible map provider,
// enabling eBPF programs to redirect packets to AF_XDP sockets via
// bpf_redirect_map().
//
// The XSKMAP relies entirely on the eBPF base array map for storage. The
// provider callbacks intercept CRUD operations to reference-count XSK handles.
// The base map stores HANDLE-sized values; the provider validates and transforms
// them on add/delete.
//

#include "precomp.h"
#include "xskmap.h"
#include "xskmap.tmh"

//
// Per-map context for an XSKMAP instance. Minimal: just tracks max_entries
// for validation.
//
typedef struct _XDP_XSKMAP_CONTEXT {
    UINT32 MaxEntries;
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
static const EBPF_EXTENSION_CLIENT *EbpfXskmapClient;

static
_Ret_maybenull_
ebpf_base_map_client_dispatch_table_t *
XdpXskmapGetClientDispatch(
    VOID
    )
{
    const EBPF_EXTENSION_CLIENT *Client = EbpfXskmapClient;
    const ebpf_extension_data_t *ClientExtData;
    const ebpf_map_client_data_t *ClientData;

    if (Client == NULL) {
        return NULL;
    }

    ClientExtData = EbpfExtensionClientGetClientData(Client);
    if (ClientExtData == NULL) {
        return NULL;
    }

    ClientData = (const ebpf_map_client_data_t *)ClientExtData;
    return ClientData->base_client_table;
}

static
ebpf_result_t
XdpXskmapProcessCreate(
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
    ebpf_base_map_client_dispatch_table_t *ClientDispatch;

    UNREFERENCED_PARAMETER(BindingContext);
    UNREFERENCED_PARAMETER(ValueSize);

    TraceEnter(
        TRACE_CORE, "MapType=%u KeySize=%u ValueSize=%u MaxEntries=%u",
        MapType, KeySize, ValueSize, MaxEntries);

    *ActualValueSize = 0;
    *MapContext = NULL;

    if (MapType != BPF_MAP_TYPE_XSKMAP) {
        return EBPF_INVALID_ARGUMENT;
    }

    if (KeySize != sizeof(UINT32)) {
        return EBPF_INVALID_ARGUMENT;
    }

    if (MaxEntries == 0) {
        return EBPF_INVALID_ARGUMENT;
    }

    //
    // The value stored in the base map is a referenced XSK handle (pointer).
    //
    *ActualValueSize = sizeof(HANDLE);

    ClientDispatch = XdpXskmapGetClientDispatch();
    if (ClientDispatch == NULL) {
        return EBPF_OPERATION_NOT_SUPPORTED;
    }

    //
    // Allocate the per-map context using epoch-protected allocation.
    //
    Context = ClientDispatch->epoch_allocate_with_tag(sizeof(*Context), XDP_POOLTAG_MAP);
    if (Context == NULL) {
        return EBPF_NO_MEMORY;
    }

    Context->MaxEntries = MaxEntries;
    *MapContext = Context;

    TraceExitSuccess(TRACE_CORE);
    return EBPF_SUCCESS;
}

static
void
XdpXskmapProcessDelete(
    _In_ void *BindingContext,
    _In_ _Post_invalid_ void *MapContext
    )
{
    XDP_XSKMAP_CONTEXT *Context = (XDP_XSKMAP_CONTEXT *)MapContext;
    ebpf_base_map_client_dispatch_table_t *ClientDispatch;

    UNREFERENCED_PARAMETER(BindingContext);

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    //
    // Note: The eBPF runtime calls process_map_delete_element for each entry
    // before calling process_map_delete, so all XSK handles are already
    // dereferenced by the time we get here.
    //

    ClientDispatch = XdpXskmapGetClientDispatch();
    if (ClientDispatch != NULL) {
        ClientDispatch->epoch_free(Context);
    }

    TraceExitSuccess(TRACE_CORE);
}

static
ebpf_result_t
XdpXskmapAssociateProgramType(
    _In_ void *BindingContext,
    _In_ void *MapContext,
    _In_ const ebpf_program_type_t *ProgramType
    )
{
    static const ebpf_program_type_t ExpectedProgramType = EBPF_PROGRAM_TYPE_XDP_INIT;

    UNREFERENCED_PARAMETER(BindingContext);
    UNREFERENCED_PARAMETER(MapContext);

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    if (!IsEqualGUID(ProgramType, &ExpectedProgramType)) {
        TraceError(TRACE_CORE, "XSKMAP only supports XDP program type");
        return EBPF_OPERATION_NOT_SUPPORTED;
    }

    TraceExitSuccess(TRACE_CORE);
    return EBPF_SUCCESS;
}

static
ebpf_result_t
XdpXskmapProcessFindElement(
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
    UNREFERENCED_PARAMETER(MapContext);
    UNREFERENCED_PARAMETER(KeySize);
    UNREFERENCED_PARAMETER(Key);
    UNREFERENCED_PARAMETER(OutValueSize);
    UNREFERENCED_PARAMETER(OutValue);
    UNREFERENCED_PARAMETER(Flags);

    //
    // The base array map handles the lookup. The in_value contains the stored
    // HANDLE. Validate it is non-NULL.
    //
    if (InValueSize != sizeof(HANDLE) || InValue == NULL) {
        return EBPF_INVALID_ARGUMENT;
    }

    if (*(const HANDLE *)InValue == NULL) {
        return EBPF_KEY_NOT_FOUND;
    }

    return EBPF_SUCCESS;
}

static
ebpf_result_t
XdpXskmapProcessAddElement(
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

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    //
    // XSKMAP updates from BPF programs are not supported. The eBPF runtime
    // enforces this because updates_original_value is set to TRUE in the
    // provider properties.
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
    NewXskHandle = NULL;
    Status = STATUS_SUCCESS;

Exit:

    if (NewXskHandle != NULL) {
        XskDereferenceDatapathHandle(NewXskHandle);
    }

    TraceExitStatus(TRACE_CORE);

    return NT_SUCCESS(Status) ? EBPF_SUCCESS : EBPF_INVALID_ARGUMENT;
}

static
ebpf_result_t
XdpXskmapProcessDeleteElement(
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
    UNREFERENCED_PARAMETER(Flags);

    TraceEnter(TRACE_CORE, "MapContext=%p", MapContext);

    //
    // The Value parameter contains the entry being removed from the base map.
    // Dereference the XSK handle stored there.
    //
    if (ValueSize == sizeof(HANDLE) && Value != NULL) {
        XskHandle = *(const HANDLE *)Value;
        if (XskHandle != NULL) {
            XskDereferenceDatapathHandle(XskHandle);
        }
    }

    TraceExitSuccess(TRACE_CORE);
    return EBPF_SUCCESS;
}

//
// Provider dispatch table for XSKMAP operations.
//
static ebpf_base_map_provider_dispatch_table_t XdpXskmapProviderDispatchTable = {
    .header = EBPF_BASE_MAP_PROVIDER_DISPATCH_TABLE_HEADER,
    .process_map_create = XdpXskmapProcessCreate,
    .process_map_delete = XdpXskmapProcessDelete,
    .associate_program_function = XdpXskmapAssociateProgramType,
    .process_map_find_element = XdpXskmapProcessFindElement,
    .process_map_add_element = XdpXskmapProcessAddElement,
    .process_map_delete_element = XdpXskmapProcessDeleteElement,
};

static ebpf_base_map_provider_properties_t XdpXskmapProviderProperties = {
    .header = EBPF_BASE_MAP_PROVIDER_PROPERTIES_HEADER,
    .updates_original_value = TRUE,
};

static ebpf_map_provider_data_t XdpXskmapProviderData = {
    .header = EBPF_MAP_PROVIDER_DATA_HEADER,
    .map_type = BPF_MAP_TYPE_XSKMAP,
    .base_map_type = BPF_MAP_TYPE_HASH,
    .base_properties = &XdpXskmapProviderProperties,
    .base_provider_table = &XdpXskmapProviderDispatchTable,
};

//
// Client attach callback: capture the client for use by bpf_redirect_map.
//
static
NTSTATUS
XdpXskmapOnClientAttach(
    _In_ const EBPF_EXTENSION_CLIENT *AttachingClient,
    _In_ const EBPF_EXTENSION_PROVIDER *AttachingProvider
    )
{
    UNREFERENCED_PARAMETER(AttachingProvider);

    TraceEnter(TRACE_CORE, "Client=%p", AttachingClient);

    EbpfXskmapClient = AttachingClient;

    TraceExitSuccess(TRACE_CORE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
XdpXskmapOnClientDetach(
    _In_ const EBPF_EXTENSION_CLIENT *DetachingClient
    )
{
    UNREFERENCED_PARAMETER(DetachingClient);

    TraceEnter(TRACE_CORE, "Client=%p", DetachingClient);

    EbpfXskmapClient = NULL;
    EbpfExtensionDetachClientCompletion((EBPF_EXTENSION_CLIENT *)DetachingClient);

    TraceExitSuccess(TRACE_CORE);
    return STATUS_SUCCESS;
}

ebpf_result_t
XdpXskmapFindElement(
    _In_ const void *Map,
    _In_ const VOID *Key,
    _Outptr_ VOID **Value
    )
{
    ebpf_base_map_client_dispatch_table_t *ClientDispatch;

    ClientDispatch = XdpXskmapGetClientDispatch();
    if (ClientDispatch == NULL) {
        return EBPF_OPERATION_NOT_SUPPORTED;
    }

    //
    // Use the eBPF runtime's map find_element to look up the value in the base
    // array map. This returns a pointer to the stored HANDLE value.
    //
    return ClientDispatch->find_element_function(Map, Key, (uint8_t **)Value);
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
