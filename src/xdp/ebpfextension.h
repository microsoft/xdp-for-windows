//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _EBPF_EXTENSION_PROVIDER EBPF_EXTENSION_PROVIDER;
typedef struct _EBPF_EXTENSION_CLIENT EBPF_EXTENSION_CLIENT;

typedef
NTSTATUS
EBPF_EXTENSION_ON_CLIENT_ATTACH(
    _In_ const EBPF_EXTENSION_CLIENT *AttachingClient,
    _In_ const EBPF_EXTENSION_PROVIDER *AttachingProvider
    );

typedef
NTSTATUS
EBPF_EXTENSION_ON_CLIENT_DETACH(
    _In_ const EBPF_EXTENSION_CLIENT *DetachingClient
    );

typedef struct _EBPF_EXTENSION_PROVIDER_PARAMETERS {
    const NPI_MODULEID *ProviderModuleId;
    const ebpf_extension_data_t *ProviderData;
} EBPF_EXTENSION_PROVIDER_PARAMETERS;

const ebpf_extension_data_t *
EbpfExtensionClientGetClientData(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    );

const ebpf_extension_dispatch_table_t *
EbpfExtensionClientGetDispatch(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    );

const ebpf_extension_program_dispatch_table_t *
EbpfExtensionClientGetProgramDispatch(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    );

const VOID *
EbpfExtensionClientGetClientContext(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    );

VOID
EbpfExtensionClientSetProviderData(
    _In_ const EBPF_EXTENSION_CLIENT *Client,
    _In_opt_ const VOID *ProviderData
    );

VOID *
EbpfExtensionClientGetProviderData(
    _In_ const EBPF_EXTENSION_CLIENT *Client
    );

VOID
EbpfExtensionDetachClientCompletion(
    _In_ EBPF_EXTENSION_CLIENT *HookClient
    );

VOID
EbpfExtensionProviderUnregister(
    _Frees_ptr_opt_ EBPF_EXTENSION_PROVIDER *ProviderContext
    );

NTSTATUS
EbpfExtensionProviderRegister(
    _In_ const NPIID *NpiId,
    _In_ const EBPF_EXTENSION_PROVIDER_PARAMETERS *Parameters,
    _In_opt_ EBPF_EXTENSION_ON_CLIENT_ATTACH *AttachCallback,
    _In_opt_ EBPF_EXTENSION_ON_CLIENT_DETACH *DetachCallback,
    _In_opt_ const VOID *CustomData,
    _Outptr_ EBPF_EXTENSION_PROVIDER **ProviderContext
    );
