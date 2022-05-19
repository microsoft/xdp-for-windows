//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

//
// {E1CADF68-E3DD-4A4A-932D-386A29399F5E}
//
const GUID NPI_XDP_ID = {
    0xE1CADF68, 0xE3DD, 0x4A4A, {0x93, 0x2D, 0x38, 0x6A, 0x29, 0x39, 0x9F, 0x5E}
};

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpGuidCreate(
    _Out_ GUID *Guid
    )
{
    NTSTATUS Status;

    Status = ExUuidCreate(Guid);
    if (Status == RPC_NT_UUID_LOCAL_ONLY) {
        //
        // The GUID is only a LUID, which is perfectly fine.
        //
        Status = STATUS_SUCCESS;
    }

    return Status;
}
