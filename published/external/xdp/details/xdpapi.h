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

//
// Parameters for creating an XDP_OBJECT_TYPE_PROGRAM.
//
typedef struct _XDP_PROGRAM_OPEN {
    UINT32 IfIndex;
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
    XDP_CREATE_PROGRAM_FLAGS Flags;
    UINT32 RuleCount;
    const XDP_RULE *Rules;
} XDP_PROGRAM_OPEN;

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

    ProgramOpen = (XDP_PROGRAM_OPEN *)
        _XdpInitializeEa(XDP_OBJECT_TYPE_PROGRAM, EaBuffer, sizeof(EaBuffer));
    ProgramOpen->IfIndex = InterfaceIndex;
    ProgramOpen->HookId = *HookId;
    ProgramOpen->QueueId = QueueId;
    ProgramOpen->Flags = Flags;
    ProgramOpen->RuleCount = RuleCount;
    ProgramOpen->Rules = Rules;

    return _XdpOpen(Program, FILE_CREATE, EaBuffer, sizeof(EaBuffer));
}

//
// Parameters for creating an XDP_OBJECT_TYPE_INTERFACE.
//
typedef struct _XDP_INTERFACE_OPEN {
    UINT32 IfIndex;
} XDP_INTERFACE_OPEN;

inline
XDP_STATUS
XdpInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ HANDLE *InterfaceHandle
    )
{
    XDP_INTERFACE_OPEN *InterfaceOpen;
    CHAR EaBuffer[XDP_OPEN_EA_LENGTH + sizeof(*InterfaceOpen)];

    InterfaceOpen = (XDP_INTERFACE_OPEN *)
        _XdpInitializeEa(XDP_OBJECT_TYPE_INTERFACE, EaBuffer, sizeof(EaBuffer));
    InterfaceOpen->IfIndex = InterfaceIndex;

    return _XdpOpen(InterfaceHandle, FILE_CREATE, EaBuffer, sizeof(EaBuffer));
}

//
// IOCTLs supported by an interface file handle.
//
#define IOCTL_INTERFACE_OFFLOAD_RSS_GET \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_RSS_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_RSS_GET_CAPABILITIES \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERFACE_OFFLOAD_QEO_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#ifdef _KERNEL_MODE

#define XDP_DRIVER_START_CALLBACK_NAME L"\\Callback\\XdpDriverStart"

inline
_IRQL_requires_max_(PASSIVE_LEVEL)
XDP_STATUS
XdpRegisterDriverStartCallback(
    _Out_ XDP_CALLBACK_HANDLE *CallbackRegistration,
    _In_ PCALLBACK_FUNCTION CallbackFunction,
    _In_opt_ VOID *CallbackContext
    )
{
    XDP_STATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING CallbackObjectName;
    PCALLBACK_OBJECT CallbackObject = NULL;

    RtlInitUnicodeString(&CallbackObjectName, XDP_DRIVER_START_CALLBACK_NAME);
    InitializeObjectAttributes(
        &ObjectAttributes, &CallbackObjectName,
        OBJ_CASE_INSENSITIVE | OBJ_PERMANENT | OBJ_KERNEL_HANDLE, NULL, NULL);

    Status = ExCreateCallback(&CallbackObject, &ObjectAttributes, TRUE, TRUE);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    *CallbackRegistration = ExRegisterCallback(CallbackObject, CallbackFunction, CallbackContext);
    if (*CallbackRegistration == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

Exit:

    if (CallbackObject != NULL) {
        //
        // The callback registration holds an implicit reference on the callback
        // object, so release the initial reference. The callback will finally
        // be cleaned up when no registrations remain.
        //
        ObDereferenceObject(CallbackObject);
    }

    return Status;
}

inline
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpDeregisterDriverStartCallback(
    _In_ XDP_CALLBACK_HANDLE CallbackRegistration
    )
{
    XDPAPI_ASSERT(CallbackRegistration != NULL);
    ExUnregisterCallback(CallbackRegistration);
}

#endif // _KERNEL_MODE

#ifdef __cplusplus
} // extern "C"
#endif

#endif
