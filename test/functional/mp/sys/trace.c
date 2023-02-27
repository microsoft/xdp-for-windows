//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "trace.tmh"

VOID
TraceNbls(
    _In_ NET_BUFFER_LIST *NblChain
    )
{
    while (NblChain != NULL) {
        TraceVerbose(TRACE_DATAPATH, "Nbl=%p", NblChain);
        NblChain = NblChain->Next;
    }
}
