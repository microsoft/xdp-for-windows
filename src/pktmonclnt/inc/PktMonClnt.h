//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Internal header for the pktmon client library.
// Public types are provided by the WDK's pktmonclntk.h and pktmonclntnpik.h.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//
// Packet monitor NMR client context
//
typedef struct _PKTMON_CLIENT_CONTEXT
{
    HANDLE  NmrClientHandle;

    PEX_RUNDOWN_REF_CACHE_AWARE RundownRef;

    BOOLEAN Enabled;

    PKTMON_CLIENT_COMP_ENUM_HANDLER   EnumComponents;
    PKTMON_CLIENT_CLEANUP_HANDLER     CleanupComponents;
    PKTMON_CLIENT_COMP_NOTIFY_HANDLER NotifyComponent;

    //
    // Provider dispatch
    //
    PVOID ProviderContext;
    PKTMON_PROVIDER_DISPATCH *ProviderDispatch;

} PKTMON_CLIENT_CONTEXT;

extern PKTMON_CLIENT_CONTEXT PktMon;

#ifdef __cplusplus
} /* extern "C" */
#endif

