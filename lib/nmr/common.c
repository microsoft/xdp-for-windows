//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

//
// {EA6A4A2B-1DE2-4A2D-86E7-6C093969425B}
//
const GUID NPI_XDP_ID = {
    0xEA6A4A2B, 0x1DE2, 0x4A2D, {0x86, 0xE7, 0x6C, 0x09, 0x39, 0x69, 0x42, 0x5B}
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
