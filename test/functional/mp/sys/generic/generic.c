//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "generic.tmh"

NDIS_STATUS
MiniportRestartHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_MINIPORT_RESTART_PARAMETERS *RestartParameters
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    UNREFERENCED_PARAMETER(RestartParameters);

    ExReInitializeRundownProtection(&Adapter->Generic->NblRundown);

    TraceExitSuccess(TRACE_CONTROL);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
MiniportPauseHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_MINIPORT_PAUSE_PARAMETERS *PauseParameters
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter->LastPauseTimestamp = KeQueryPerformanceCounter(NULL);
    ExWaitForRundownProtectionRelease(&Adapter->Generic->NblRundown);

    TraceExitSuccess(TRACE_CONTROL);

    return NDIS_STATUS_SUCCESS;
}

VOID
MiniportCancelSendHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ VOID *CancelId
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpGetMiniportPauseTimestamp(
    _In_ GENERIC_CONTEXT *Generic,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    LARGE_INTEGER Timestamp = Generic->Adapter->LastPauseTimestamp;

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Timestamp)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    *(LARGE_INTEGER *)Irp->AssociatedIrp.SystemBuffer = Timestamp;
    Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = sizeof(Timestamp);

Exit:

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpSetMtu(
    _In_ GENERIC_CONTEXT *Generic,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    CONST MINIPORT_SET_MTU_IN *In = Irp->AssociatedIrp.SystemBuffer;
    UINT32 NewMtu;
    NDIS_STATUS_INDICATION Indication = {0};
    NTSTATUS Status;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->Mtu < FNMP_MIN_MTU || In->Mtu > FNMP_MAX_MTU) {
        //
        // Existing components (e.g. LLDP) perform dangerous arithmetic on
        // MTU values. Ensure the MTU is within tested-safe bounds.
        //
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // The MTU provided to NDIS does not include the Ethernet header length.
    // The FNMP API specifies the MTU as the total frame length, so adjust
    // for the Ethernet size here.
    //
    NewMtu = In->Mtu - ETH_HDR_LEN;
    Generic->Adapter->MtuSize = NewMtu;

    //
    // Use an undocumented API to change the MTU of an initialized miniport.
    // Unfortunately there is no documented mechanism for NDIS6 adapters.
    //
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(Indication);
    Indication.SourceHandle = Generic->Adapter->MiniportHandle;
    Indication.StatusCode = NDIS_STATUS_L2_MTU_SIZE_CHANGE;
    Indication.StatusBuffer = &NewMtu;
    Indication.StatusBufferSize = sizeof(NewMtu);
    NdisMIndicateStatusEx(Generic->Adapter->MiniportHandle, &Indication);
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

VOID
GenericAdapterCleanup(
    _In_ ADAPTER_GENERIC *AdapterGeneric
    )
{
    if (AdapterGeneric->NblPool != NULL) {
        NdisFreeNetBufferListPool(AdapterGeneric->NblPool);
    }

    ExFreePoolWithTag(AdapterGeneric, POOLTAG_GENERIC);
}

ADAPTER_GENERIC *
GenericAdapterCreate(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    ADAPTER_GENERIC *AdapterGeneric;
    NTSTATUS Status;

    AdapterGeneric = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*AdapterGeneric), POOLTAG_GENERIC);
    if (AdapterGeneric == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    AdapterGeneric->Adapter = Adapter;
    ExInitializeRundownProtection(&AdapterGeneric->NblRundown);
    ExWaitForRundownProtectionRelease(&AdapterGeneric->NblRundown);
    KeInitializeSpinLock(&AdapterGeneric->Lock);
    InitializeListHead(&AdapterGeneric->TxFilterList);

    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams = {0};
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = POOLTAG_GENERIC_RX;
    PoolParams.ContextSize = FNIO_ENQUEUE_NBL_CONTEXT_SIZE;

    AdapterGeneric->NblPool = NdisAllocateNetBufferListPool(NULL, &PoolParams);
    if (AdapterGeneric->NblPool == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (AdapterGeneric != NULL) {
            GenericAdapterCleanup(AdapterGeneric);
            AdapterGeneric = NULL;
        }
    }

    return AdapterGeneric;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    GENERIC_CONTEXT *Generic = IrpSp->FileObject->FsContext;
    NTSTATUS Status;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {

    case IOCTL_RX_ENQUEUE:
        Status = GenericIrpRxEnqueue(Generic->Rx, Irp, IrpSp);
        break;

    case IOCTL_RX_FLUSH:
        Status = GenericIrpRxFlush(Generic->Rx, Irp, IrpSp);
        break;

    case IOCTL_TX_FILTER:
        Status = GenericIrpTxFilter(Generic->Tx, Irp, IrpSp);
        break;

    case IOCTL_TX_GET_FRAME:
        Status = GenericIrpTxGetFrame(Generic->Tx, Irp, IrpSp);
        break;

    case IOCTL_TX_DEQUEUE_FRAME:
        Status = GenericIrpTxDequeueFrame(Generic->Tx, Irp, IrpSp);
        break;

    case IOCTL_TX_FLUSH:
        Status = GenericIrpTxFlush(Generic->Tx, Irp, IrpSp);
        break;

    case IOCTL_MINIPORT_PAUSE_TIMESTAMP:
        Status = GenericIrpGetMiniportPauseTimestamp(Generic, Irp, IrpSp);
        break;

    case IOCTL_MINIPORT_SET_MTU:
        Status = GenericIrpSetMtu(Generic, Irp, IrpSp);
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
GenericCleanup(
    _In_ GENERIC_CONTEXT *Generic
    )
{
    if (Generic->Tx != NULL) {
        GenericTxCleanup(Generic->Tx);
    }

    if (Generic->Rx != NULL) {
        GenericRxCleanup(Generic->Rx);
    }

    if (Generic->Adapter != NULL) {
        MpDereferenceAdapter(Generic->Adapter);
    }

    ExFreePoolWithTag(Generic, POOLTAG_GENERIC);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
GenericIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    UNREFERENCED_PARAMETER(Irp);

    GenericCleanup(IrpSp->FileObject->FsContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH GenericFileDispatch = {
    .IoControl = GenericIrpDeviceIoControl,
    .Close = GenericIrpClose,
};

NTSTATUS
GenericIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    GENERIC_CONTEXT *Generic = NULL;
    XDPFNMP_OPEN_GENERIC *OpenGeneric;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);

    if (InputBufferLength < sizeof(*OpenGeneric)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }
    OpenGeneric = InputBuffer;

    Generic = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Generic), POOLTAG_GENERIC);
    if (Generic == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Generic->Header.ObjectType = XDPFNMP_FILE_TYPE_GENERIC;
    Generic->Header.Dispatch = &GenericFileDispatch;
    KeInitializeSpinLock(&Generic->Lock);

    Generic->Adapter = MpFindAdapter(OpenGeneric->IfIndex);
    if (Generic->Adapter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Generic->Rx = GenericRxCreate(Generic);
    if (Generic->Rx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Generic->Tx = GenericTxCreate(Generic);
    if (Generic->Tx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    IrpSp->FileObject->FsContext = Generic;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Generic != NULL) {
            GenericCleanup(Generic);
        }
    }

    return Status;
}
