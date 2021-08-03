//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

NTSTATUS
NdisPollGetBackchannel(
    _Inout_ IRP *Irp,
    _Out_ CONST NDIS_POLL_BACKCHANNEL_DISPATCH **Dispatch
    );
