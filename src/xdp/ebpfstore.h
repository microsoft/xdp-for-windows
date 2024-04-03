//
// Copyright (c) Microsoft Corporation.
//

#pragma once

#include <ebpf_extension.h>
#include <ebpf_extension_uuids.h>
#include <ebpf_nethooks.h>
#include <ebpf_program_attach_type_guids.h>
#include <ebpf_program_types.h>
#include <ebpf_structs.h>
#include <ebpf_private_extension.h>
#include <ebpf_windows.h>

#define EBPF_OFFSET_OF(s, m) (((size_t) & ((s*)0)->m))
#define EBPF_FIELD_SIZE(s, m) (sizeof(((s*)0)->m))
#define EBPF_SIZE_INCLUDING_FIELD(s, m) (EBPF_OFFSET_OF(s, m) + EBPF_FIELD_SIZE(s, m))
#define EBPF_EXTENSION_DATA_CURRENT_VERSION 1
#define EBPF_EXTENSION_DATA_CURRENT_VERSION_SIZE sizeof(ebpf_extension_data_t)
#define EBPF_ATTACH_PROVIDER_DATA_CURRENT_VERSION_SIZE sizeof(ebpf_attach_provider_data_t)
#define EBPF_HELPER_FUNCTION_ADDRESSES_CURRENT_VERSION_SIZE sizeof(ebpf_helper_function_addresses_t)

static const ebpf_context_descriptor_t EbpfXdpContextDescriptor = {
    .size = sizeof(xdp_md_t),
    .data = FIELD_OFFSET(xdp_md_t, data),
    .end = FIELD_OFFSET(xdp_md_t, data_end),
    .meta = FIELD_OFFSET(xdp_md_t, data_meta),
};

#define XDP_EXT_HELPER_FUNCTION_START EBPF_MAX_GENERAL_HELPER_FUNCTION

// XDP helper function prototype descriptors.
static const ebpf_helper_function_prototype_t EbpfXdpHelperFunctionPrototype[] = {
    {
        .header = {
            .version = EBPF_HELPER_FUNCTION_PROTOTYPE_CURRENT_VERSION,
            .size = EBPF_HELPER_FUNCTION_PROTOTYPE_CURRENT_VERSION_SIZE
        },
        .helper_id = XDP_EXT_HELPER_FUNCTION_START + 1,
        .name = "bpf_xdp_adjust_head",
        .return_type = EBPF_RETURN_TYPE_INTEGER,
        .arguments = {
            EBPF_ARGUMENT_TYPE_PTR_TO_CTX,
            EBPF_ARGUMENT_TYPE_ANYTHING,
        },
    },
};

static const ebpf_program_type_descriptor_t EbpfXdpProgramTypeDescriptor = {
    .header = {
        .version = EBPF_PROGRAM_TYPE_DESCRIPTOR_CURRENT_VERSION,
        .size = EBPF_PROGRAM_TYPE_DESCRIPTOR_CURRENT_VERSION_SIZE
    },
    .name = "xdp",
    .context_descriptor = &EbpfXdpContextDescriptor,
    .program_type = EBPF_PROGRAM_TYPE_XDP_INIT,
    .bpf_prog_type = BPF_PROG_TYPE_XDP
};

const ebpf_program_info_t EbpfXdpProgramInfo = {
    .header = {
        .version = EBPF_PROGRAM_INFORMATION_CURRENT_VERSION,
        .size = EBPF_PROGRAM_INFORMATION_CURRENT_VERSION_SIZE
    },
    .program_type_descriptor = &EbpfXdpProgramTypeDescriptor,
    .count_of_program_type_specific_helpers = RTL_NUMBER_OF(EbpfXdpHelperFunctionPrototype),
    .program_type_specific_helper_prototype = EbpfXdpHelperFunctionPrototype,
};

#define DECLARE_XDP_SECTION(_section_name) \
    { EBPF_PROGRAM_SECTION_INFORMATION_CURRENT_VERSION, EBPF_PROGRAM_SECTION_INFORMATION_CURRENT_VERSION_SIZE }, \
    (const wchar_t*)_section_name, &EBPF_PROGRAM_TYPE_XDP, &EBPF_ATTACH_TYPE_XDP, \
    BPF_PROG_TYPE_XDP, BPF_XDP

const ebpf_program_section_info_t DECLSPEC_SELECTANY EbpfXdpSectionInfo[] = {
    {
        DECLARE_XDP_SECTION(L"xdp")
    }
};