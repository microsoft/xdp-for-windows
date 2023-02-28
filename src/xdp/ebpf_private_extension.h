//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// The following definitions are from ebpf-for-windows but are not defined in
// its "include" directory.
//

#define EBPF_MAX_GENERAL_HELPER_FUNCTION 0xFFFF

typedef struct _ebpf_attach_provider_data
{
    ebpf_program_type_t supported_program_type;
    bpf_attach_type_t bpf_attach_type;
    enum bpf_link_type link_type;
} ebpf_attach_provider_data_t;

#define EBPF_ATTACH_PROVIDER_DATA_VERSION 1

typedef struct _ebpf_extension_data
{
    uint16_t version;
    size_t size;
    void* data;
} ebpf_extension_data_t;

typedef ebpf_result_t (*ebpf_invoke_program_function_t)(
    _In_ const void* client_binding_context, _In_ const void* context, _Out_ uint32_t* result);


typedef ebpf_result_t
(*ebpf_invoke_batch_begin_function_t)(
    _In_ const void* extension_client_binding_context, size_t state_size, _Out_writes_(state_size) void* state);

typedef ebpf_result_t
(*ebpf_invoke_program_batch_function_t)(
    _In_ const void* extension_client_binding_context,
    _Inout_ void* program_context,
    _Out_ uint32_t* result,
    _In_ const void* state);

typedef ebpf_result_t
(*ebpf_invoke_batch_end_function_t)(_In_ const void* extension_client_binding_context);

typedef ebpf_result_t (*_ebpf_extension_dispatch_function)();

typedef struct _ebpf_extension_dispatch_table
{
    uint16_t version;
    uint16_t size;
    _ebpf_extension_dispatch_function function[1];
} ebpf_extension_dispatch_table_t;

//
// The following definitions are proposed to be upstreamed:
//

#define EBPF_PROGRAM_TYPE_XDP_INIT { \
    0xf1832a85, \
    0x85d5, \
    0x45b0, \
    {0x98, 0xa0, 0x70, 0x69, 0xd6, 0x30, 0x13, 0xb0}}

#define EBPF_ATTACH_TYPE_XDP_INIT { \
    0x85e0d8ef, \
    0x579e, \
    0x4931, \
    {0xb0, 0x72, 0x8e, 0xe2, 0x26, 0xbb, 0x2e, 0x9d}}
