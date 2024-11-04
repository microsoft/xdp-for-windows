//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_XDPAPI_H
#define XDP_DETAILS_XDPAPI_H

#include <xdpapi.h>
#include <xdp/status.h>
#include <xdp/details/ioctldef.h>
#include <xdp/details/ioctlfn.h>

#ifdef __cplusplus
extern "C" {
#endif

inline
XDP_STATUS
XdpCreateProgram(
    _In_ UINT32 InterfaceIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags,
    _In_reads_(RuleCount) const XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _Out_ HANDLE *Program
    )
{
    XDP_PROGRAM_OPEN *ProgramOpen;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH + sizeof(*ProgramOpen)];
    XDP_STATUS Res;

    ProgramOpen = XdpInitializeEa(XDP_OBJECT_TYPE_PROGRAM, EaBuffer, sizeof(EaBuffer));
    ProgramOpen->IfIndex = InterfaceIndex;
    ProgramOpen->HookId = *HookId;
    ProgramOpen->QueueId = QueueId;
    ProgramOpen->Flags = Flags;
    ProgramOpen->RuleCount = RuleCount;
    ProgramOpen->Rules = Rules;

    return _XdpOpen(Program, FILE_CREATE, EaBuffer, sizeof(EaBuffer));
}

inline
XDP_STATUS
XdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    XDP_INTERFACE_OPEN *InterfaceOpen;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH + sizeof(*InterfaceOpen)];

    InterfaceOpen =
        XdpInitializeEa(XDP_OBJECT_TYPE_INTERFACE, EaBuffer, sizeof(EaBuffer));
    InterfaceOpen->IfIndex = InterfaceIndex;

    return _XdpOpen(InterfaceHandle, FILE_CREATE, EaBuffer, sizeof(EaBuffer));
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif
