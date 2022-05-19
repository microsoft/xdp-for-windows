//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
AdapterIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    ADAPTER_USER_CONTEXT *UserContext = IrpSp->FileObject->FsContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_OID_FILTER:
        Status = MpIrpOidSetFilter(UserContext->Adapter, Irp, IrpSp);
        break;

    case IOCTL_OID_GET_REQUEST:
        Status = MpIrpOidGetRequest(UserContext->Adapter, Irp, IrpSp);
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
AdapterCleanup(
    _In_ ADAPTER_USER_CONTEXT *UserContext
    )
{
    if (UserContext->Adapter != NULL) {
        if (UserContext->SetOidFilter) {
            MpOidCompleteRequest(UserContext->Adapter);
            MpOidClearFilter(UserContext->Adapter);
        }

        RtlAcquirePushLockExclusive(&UserContext->Adapter->Lock);
        if (UserContext->Adapter->UserContext == UserContext) {
            UserContext->Adapter->UserContext = NULL;
        }
        RtlReleasePushLockExclusive(&UserContext->Adapter->Lock);

        MpDereferenceAdapter(UserContext->Adapter);
    }

    ExFreePoolWithTag(UserContext, POOLTAG_ADAPTER);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
AdapterIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    ADAPTER_USER_CONTEXT *UserContext = (ADAPTER_USER_CONTEXT *)IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    ASSERT(UserContext == UserContext->Adapter->UserContext);

    AdapterCleanup(UserContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH AdapterFileDispatch = {
    .IoControl = AdapterIrpDeviceIoControl,
    .Close = AdapterIrpClose,
};

NTSTATUS
AdapterIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    ADAPTER_USER_CONTEXT *UserContext = NULL;
    XDPFNMP_OPEN_ADAPTER *OpenAdapter;
    UINT32 IfIndex;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);

    if (InputBufferLength < sizeof(*OpenAdapter)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }
    OpenAdapter = InputBuffer;
    IfIndex = OpenAdapter->IfIndex;

    UserContext = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*UserContext), POOLTAG_ADAPTER);
    if (UserContext == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    UserContext->Header.ObjectType = XDPFNMP_FILE_TYPE_ADAPTER;
    UserContext->Header.Dispatch = &AdapterFileDispatch;

    UserContext->Adapter = MpFindAdapter(IfIndex);
    if (UserContext->Adapter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&UserContext->Adapter->Lock);

    if (UserContext->Adapter->UserContext != NULL) {
        Status = STATUS_DUPLICATE_OBJECTID;
        RtlReleasePushLockExclusive(&UserContext->Adapter->Lock);
        goto Exit;
    }

    UserContext->Adapter->UserContext = UserContext;

    RtlReleasePushLockExclusive(&UserContext->Adapter->Lock);

    IrpSp->FileObject->FsContext = UserContext;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (UserContext != NULL) {
            AdapterCleanup(UserContext);
        }
    }

    return Status;
}
