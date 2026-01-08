//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDPAPI_H
#define XDPAPI_H

#ifdef __cplusplus
extern "C" {
#endif

//
// Include all necessary Windows headers first.
//
#include <xdp/wincommon.h>

#include <xdp/apiversion.h>
#include <xdp/hookid.h>
#include <xdp/objectheader.h>
#include <xdp/program.h>
#include <xdp/status.h>

typedef enum _XDP_CREATE_PROGRAM_FLAGS {
    XDP_CREATE_PROGRAM_FLAG_NONE = 0x0,
    XDP_CREATE_PROGRAM_FLAG_GENERIC = 0x1,
    XDP_CREATE_PROGRAM_FLAG_NATIVE = 0x2,
    XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES = 0x4,
} XDP_CREATE_PROGRAM_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(XDP_CREATE_PROGRAM_FLAGS);
static_assert(
    sizeof(XDP_CREATE_PROGRAM_FLAGS) == sizeof(UINT32),
    "== sizeof(UINT32)");

#if !defined(XDP_API_VERSION) || (XDP_API_VERSION <= XDP_API_VERSION_2)
#include <xdp/xdpapi_v1.h>
#else

XDP_STATUS
XdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    );

XDP_STATUS
XdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    );

#ifdef _KERNEL_MODE

DECLARE_HANDLE(XDP_CALLBACK_HANDLE);

_IRQL_requires_max_(PASSIVE_LEVEL)
XDP_STATUS
XdpRegisterDriverStartCallback(
    _Out_ XDP_CALLBACK_HANDLE *CallbackRegistration,
    _In_ PCALLBACK_FUNCTION CallbackFunction,
    _In_opt_ VOID *CallbackContext
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpDeregisterDriverStartCallback(
    _In_ XDP_CALLBACK_HANDLE CallbackRegistration
    );

#endif

#include <xdp/details/xdpapi.h>
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
