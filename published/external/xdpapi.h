//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#ifndef XDPAPI_H
#define XDPAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xdp/hookid.h>
#include <xdp/program.h>

#ifndef XDPAPI
#define XDPAPI __declspec(dllimport)
#endif

//
// Create and attach an XDP program to an interface. The caller may optionally
// specify generic or native XDP binding mode. See xdp/program.h for placeholder
// program definitions.
//
// N.B. The current implementation supports only L2 RX inspect programs.
//

#define XDP_ATTACH_GENERIC  0x1
#define XDP_ATTACH_NATIVE   0x2

HRESULT
XDPAPI
XdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ UINT32 Flags,
    _In_reads_(RuleCount) CONST XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    );

#ifdef __cplusplus
} // extern "C"
#endif

#endif
