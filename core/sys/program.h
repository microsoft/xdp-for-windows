//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "redirect.h"

typedef struct _XDP_PROGRAM XDP_PROGRAM;

//
// Data path routines.
//

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_RX_ACTION
XdpInspect(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_REDIRECT_CONTEXT *RedirectContext,
    _In_ XDP_RING *FrameRing,
    _In_ UINT32 FrameIndex,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID *
XdpProgramGetXskBypassTarget(
    _In_ XDP_PROGRAM *Program
    );

//
// Control path routines.
//

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
XdpProgramCanXskBypass(
    _In_ XDP_PROGRAM *Program
    );

XDP_FILE_CREATE_ROUTINE XdpIrpCreateProgram;
