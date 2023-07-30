//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static const UINT8 DummyPortSet[(UINT16_MAX + 1) / RTL_BITS_OF(UINT8)] = "Bogus values";

VOID
XdpProgramReleasePortSet(
    _Inout_ XDP_PORT_SET *PortSet
    )
{
    UNREFERENCED_PARAMETER(PortSet);
}

NTSTATUS
XdpProgramCapturePortSet(
    _In_ CONST XDP_PORT_SET *UserPortSet,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Inout_ XDP_PORT_SET *KernelPortSet
    )
{
    UNREFERENCED_PARAMETER(UserPortSet);
    UNREFERENCED_PARAMETER(RequestorMode);

    KernelPortSet->PortSet = DummyPortSet;

    return STATUS_SUCCESS;
}
