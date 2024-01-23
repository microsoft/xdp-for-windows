//
// Copyright (c) Microsoft Corporation.
//

#include <stdio.h>
#include <windows.h>
//
// Work around various eBPF compilation bugs.
//
#define USER_MODE 1
#pragma warning(push)
#pragma warning(disable:4005) // 'WIN32_LEAN_AND_MEAN': macro redefinition
#include <ebpf_store_helper.h>
#include <ebpfstore.h>
#pragma warning(pop)
#undef USER_MODE

INT
__cdecl
main(
    INT argc,
    CHAR *argv[]
    )
{
    ebpf_result_t result;
    int exit_code = 0;

    if (argc == 2 && !_strcmpi("--clear", argv[1])) {
        for (uint32_t i = 0; i < RTL_NUMBER_OF(EbpfXdpSectionInfo); i++) {
            result = ebpf_store_delete_section_information(&EbpfXdpSectionInfo[i]);
            if (result != EBPF_SUCCESS) {
                fprintf(stderr, "ebpf_store_delete_section_information failed: %u\n", result);
                exit_code = -1;
            }
        }

        result = ebpf_store_delete_program_information(&EbpfXdpProgramInfo);
        if (result != EBPF_SUCCESS) {
            fprintf(stderr, "ebpf_store_delete_program_information failed: %u\n", result);
            exit_code = -1;
        }
    } else {
        result = ebpf_store_update_section_information(&EbpfXdpSectionInfo[0], RTL_NUMBER_OF(EbpfXdpSectionInfo));
        if (result != EBPF_SUCCESS) {
            fprintf(stderr, "ebpf_store_update_section_information failed: %u\n", result);
            exit_code = -1;
        }

        result = ebpf_store_update_program_information(&EbpfXdpProgramInfo, 1);
        if (result != EBPF_SUCCESS) {
            fprintf(stderr, "ebpf_store_update_program_information failed: %u\n", result);
            exit_code = -1;
        }
    }

    return exit_code;
}
