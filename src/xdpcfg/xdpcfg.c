//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <xdp/wincommon.h>
#include <xdp/details/ioctldef.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>

static
VOID
Usage(
    VOID
    )
{
    fprintf(stderr,
        "Usage: xdpcfg.exe <Command> [OPTIONS ...]\n"
        "\n"
        "COMMANDS:\n"
        "\n"
        "    SetDeviceSddl <SDDL>\n"
        "        Set the SDDL on the common XDP device.\n"
        "\n"
        "    SetDeviceSddl <ObjectType> <SDDL>\n"
        "        Set the SDDL on a per-object-type XDP device.\n"
        "        ObjectType: program | xsk | interface\n");
    exit(EXIT_FAILURE);
}

static
const GUID *
ParseObjectTypeGuid(
    _In_ const WCHAR *ObjectTypeName
    )
{
    if (!_wcsicmp(ObjectTypeName, L"program")) {
        return &XDP_PROGRAM_DEVICE_CLASS_GUID;
    } else if (!_wcsicmp(ObjectTypeName, L"xsk")) {
        return &XDP_XSK_DEVICE_CLASS_GUID;
    } else if (!_wcsicmp(ObjectTypeName, L"interface")) {
        return &XDP_INTERFACE_DEVICE_CLASS_GUID;
    } else {
        return NULL;
    }
}

static
INT
SetDeviceSddlForGuid(
    _In_ const GUID *DeviceClassGuid,
    _In_ const WCHAR *SddlString
    )
{
    SIZE_T StringLength;

    StringLength = wcslen(SddlString) * sizeof(WCHAR) + sizeof(UNICODE_NULL);
    if (StringLength > MAXDWORD) {
        fprintf(stderr, "Integer overflow\n");
        return EXIT_FAILURE;
    }

    if (!SetupDiSetClassRegistryPropertyW(
            DeviceClassGuid, SPCRP_SECURITY_SDS, (BYTE *)SddlString, (DWORD)StringLength,
            NULL, NULL)) {
        fprintf(stderr, "SetupDiSetClassRegistryPropertyW failed: 0x%x\n", GetLastError());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static
INT
SetDeviceSddl(
    _In_ INT ArgC,
    _In_ WCHAR **ArgV
    )
{
    if (ArgC < 3) {
        Usage();
    }

    if (ArgC == 3) {
        //
        // Legacy: SetDeviceSddl <SDDL> - applies to the common XDP device.
        //
        return SetDeviceSddlForGuid(&XDP_DEVICE_CLASS_GUID, ArgV[2]);
    }

    //
    // SetDeviceSddl <ObjectType> <SDDL>
    //
    {
        const GUID *DeviceClassGuid = ParseObjectTypeGuid(ArgV[2]);
        if (DeviceClassGuid == NULL) {
            fprintf(stderr, "Unknown object type: %ls\n", ArgV[2]);
            Usage();
        }

        return SetDeviceSddlForGuid(DeviceClassGuid, ArgV[3]);
    }
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
