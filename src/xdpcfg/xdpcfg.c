//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>

#include <xdpioctl.h>

static
VOID
Usage(
    VOID
    )
{
    fprintf(stderr,
        "Usage: xdpcfg.exe <SetDeviceSddl> [OPTIONS ...]\n"
        "\n"
        "OPTIONS:\n"
        "\n"
        "    SetDeviceSddl <SDDL>\n");
    exit(EXIT_FAILURE);
}

static
INT
SetDeviceSddl(
    _In_ INT ArgC,
    _In_ WCHAR **ArgV
    )
{
    SIZE_T StringLength;

    if (ArgC < 3) {
        Usage();
    }

    StringLength = wcslen(ArgV[2]) * sizeof(WCHAR) + sizeof(UNICODE_NULL);
    if (StringLength > MAXDWORD) {
        fprintf(stderr, "Integer overflow\n");
        return EXIT_FAILURE;
    }

    if (!SetupDiSetClassRegistryPropertyW(
            &XDP_DEVICE_CLASS_GUID, SPCRP_SECURITY_SDS, (BYTE *)ArgV[2], (DWORD)StringLength,
            NULL, NULL)) {
        fprintf(stderr, "SetupDiSetClassRegistryPropertyW failed: 0x%x\n", GetLastError());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

INT
__cdecl
wmain(
    INT ArgC,
    WCHAR **ArgV
    )
{
    if (ArgC < 2) {
        Usage();
    }

    if (!_wcsicmp(ArgV[1], L"SetDeviceSddl")) {
        return SetDeviceSddl(ArgC, ArgV);
    } else {
        Usage();
    }
}
