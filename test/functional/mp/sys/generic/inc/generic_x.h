//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <dispatch.h>
#include <miniport.h>

MINIPORT_RESTART MiniportRestartHandler;
MINIPORT_PAUSE MiniportPauseHandler;
MINIPORT_SEND_NET_BUFFER_LISTS MpSendNetBufferLists;
MINIPORT_CANCEL_SEND MiniportCancelSendHandler;
MINIPORT_RETURN_NET_BUFFER_LISTS MpReturnNetBufferLists;

FILE_CREATE_ROUTINE GenericIrpCreate;

ADAPTER_GENERIC *
GenericAdapterCreate(
    _In_ ADAPTER_CONTEXT *Adapter
    );

VOID
GenericAdapterCleanup(
    _In_ ADAPTER_GENERIC *AdapterGeneric
    );
