//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

NTSTATUS
NdisPollGetBackchannel(
    _Inout_ IRP *Irp,
    _Out_ const NDIS_POLL_BACKCHANNEL_DISPATCH **Dispatch
    );
