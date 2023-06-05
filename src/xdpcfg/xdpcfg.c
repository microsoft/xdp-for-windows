//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>

VOID
Usage(
    VOID
    )
{
    fprintf(stderr, "Usage: TODO\n");
    exit(EXIT_FAILURE);
}

INT
AddXdpSid(
    INT ArgC,
    WCHAR **ArgV
    )
{
    SID Sid;
    HANDLE CfgMutex = NULL;

    if (ArgC < 3) {
        Usage();
    }

    if (!ConvertStringSidToSidW(ArgV[2], &Sid)) {
        fprintf(stderr, "Invalid SID\n");
        Usage();
    }

    //
    // TODO: protect with a mutex? how do we secure the mutex?
    //

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

    if (!_wcsicmp(ArgV[1], L"AddXdpSid")) {
        return AddXdpSid(ArgC, ArgV);
    } else {
        Usage();
    }

    return EXIT_SUCCESS;
}
