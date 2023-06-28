//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "xdpregistry.tmh"

__declspec(code_seg("PAGE"))
NTSTATUS
XdpRegQueryDwordValue(
    _In_z_ CONST WCHAR *KeyName,
    _In_z_ CONST WCHAR *ValueName,
    _Out_ ULONG *ValueData
    )
{
#define WORK_BUFFER_SIZE  512

    NTSTATUS Status;
    HANDLE KeyHandle;
    UNICODE_STRING UnicodeName;
    OBJECT_ATTRIBUTES ObjectAttributes = {0};
    UCHAR InformationBuffer[WORK_BUFFER_SIZE] = {0};
    KEY_VALUE_FULL_INFORMATION *Information = (KEY_VALUE_FULL_INFORMATION *) InformationBuffer;
    ULONG ResultLength;

    PAGED_CODE();

    RtlInitUnicodeString(&UnicodeName, KeyName);

    InitializeObjectAttributes(
        &ObjectAttributes,
        &UnicodeName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    RtlInitUnicodeString(&UnicodeName, ValueName);

    Status =
        ZwQueryValueKey(
            KeyHandle,
            &UnicodeName,
            KeyValueFullInformation,
            Information,
            WORK_BUFFER_SIZE,
            &ResultLength);
    if (NT_SUCCESS(Status)) {
        if (Information->Type != REG_DWORD) {
            Status = STATUS_INVALID_PARAMETER_MIX;
        } else {
            *ValueData = *((ULONG UNALIGNED *)((CHAR *)Information + Information->DataOffset));
        }
    }

    ZwClose(KeyHandle);

Exit:

    TraceInfo(
        TRACE_RTL,
        "KeyName=%S ValueName=%S Value=%u Status=%!STATUS!",
        KeyName, ValueName, NT_SUCCESS(Status) ? *ValueData : 0, Status);

    return Status;
}

__declspec(code_seg("PAGE"))
NTSTATUS
XdpRegQueryBoolean(
    _In_z_ CONST WCHAR *KeyName,
    _In_z_ CONST WCHAR *ValueName,
    _Out_ BOOLEAN *ValueData
    )
{
    DWORD Value;
    NTSTATUS Status;

    PAGED_CODE();

    Status = XdpRegQueryDwordValue(KeyName, ValueName, &Value);
    if (NT_SUCCESS(Status)) {
        *ValueData = !!Value;
    }

    return Status;
}

typedef struct _XDP_REG_WATCHER {
    HANDLE Key;
    EX_PUSH_LOCK Lock;
    LIST_ENTRY ClientList;
    WORK_QUEUE_ITEM WorkQueueItem;
    XDP_TIMER *FallbackTimer;
    KEVENT *DeletedEvent;
    BOOLEAN IsRegistered;
    BOOLEAN IsDeleting;
} XDP_REG_WATCHER;

static WORKER_THREAD_ROUTINE XdpRegWatcherNotify;

#define XDP_REG_WATCHER_TIMEOUT_MS 1000

static
_IRQL_requires_(PASSIVE_LEVEL)
_Requires_lock_held_(&Watcher->Lock)
NTSTATUS
XdpRegWatcherRegister(
    _In_ XDP_REG_WATCHER *Watcher
    )
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;

    ExInitializeWorkItem(&Watcher->WorkQueueItem, XdpRegWatcherNotify, Watcher);

    Status =
        ZwNotifyChangeKey(
            Watcher->Key, NULL, (PIO_APC_ROUTINE)&Watcher->WorkQueueItem,
            (VOID *)(UINT_PTR)DelayedWorkQueue, &IoStatus,
            REG_NOTIFY_CHANGE_LAST_SET, FALSE, NULL, 0, TRUE);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Watcher->IsRegistered = TRUE;

Exit:

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(WORKER_THREAD_ROUTINE)
VOID
XdpRegWatcherNotify(
    _In_ VOID *Context
    )
{
    NTSTATUS Status;
    LIST_ENTRY *Entry;
    XDP_REG_WATCHER *Watcher = Context;
    KEVENT *DeletedEvent = NULL;

    TraceEnter(TRACE_RTL, "Watcher=%p", Watcher);

    RtlAcquirePushLockExclusive(&Watcher->Lock);

    //
    // ZwNotifyChangeKey is single shot registration. Re-register below.
    //
    Watcher->IsRegistered = FALSE;

    if (Watcher->IsDeleting) {
        DeletedEvent = Watcher->DeletedEvent;
    } else {
        Status = XdpRegWatcherRegister(Watcher);
        if (!NT_SUCCESS(Status)) {
            TraceWarn(
                TRACE_RTL,
                "Failed to re-register Watcher=%p Status=%!STATUS!",
                Watcher, Status);

            //
            // We failed to register for the next one-shot notification, so try
            // again in a while. The failure should be transient.
            //
            XdpTimerStart(Watcher->FallbackTimer, XDP_REG_WATCHER_TIMEOUT_MS, NULL);
        }

        Entry = Watcher->ClientList.Flink;
        while (Entry != &Watcher->ClientList) {
            XDP_REG_WATCHER_CLIENT_ENTRY *ClientEntry =
                CONTAINING_RECORD(Entry, XDP_REG_WATCHER_CLIENT_ENTRY, Link);
            Entry = Entry->Flink;
            ClientEntry->Callback();
        }
    }

    RtlReleasePushLockExclusive(&Watcher->Lock);

    if (DeletedEvent != NULL) {
        KeSetEvent(DeletedEvent, 0, FALSE);
    }

    TraceExitSuccess(TRACE_RTL);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpRegWatcherAddClient(
    _In_ XDP_REG_WATCHER *Watcher,
    _In_ XDP_REG_WATCHER_CLIENT_CALLBACK *ClientCallback,
    _Inout_ XDP_REG_WATCHER_CLIENT_ENTRY *ClientEntry
    )
{
    ClientEntry->Callback = ClientCallback;

    RtlAcquirePushLockExclusive(&Watcher->Lock);
    ASSERT(ClientEntry->Link.Flink == NULL);
    InsertTailList(&Watcher->ClientList, &ClientEntry->Link);
    RtlReleasePushLockExclusive(&Watcher->Lock);

    //
    // Perform an initial callback after registration.
    //
    ClientCallback();
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpRegWatcherRemoveClient(
    _In_ XDP_REG_WATCHER *Watcher,
    _Inout_ XDP_REG_WATCHER_CLIENT_ENTRY *ClientEntry
    )
{
    //
    // Remove the client from the watcher list. If the client has been
    // initialized to zero, this routine performs no action. Idempotent.
    //

    if (ClientEntry->Link.Flink != NULL) {
        RtlAcquirePushLockExclusive(&Watcher->Lock);
        RemoveEntryList(&ClientEntry->Link);
        RtlZeroMemory(ClientEntry, sizeof(*ClientEntry));
        RtlReleasePushLockExclusive(&Watcher->Lock);
    }
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(WORKER_THREAD_ROUTINE)
VOID
XdpRegWatcherTimeout(
    _In_ VOID *Parameter
    )
{
    XdpRegWatcherNotify(Parameter);
}

_IRQL_requires_(PASSIVE_LEVEL)
XDP_REG_WATCHER *
XdpRegWatcherCreate(
    _In_z_ CONST WCHAR *KeyName,
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    )
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING UnicodeName;
    XDP_REG_WATCHER *Watcher = NULL;

    Watcher = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Watcher), XDP_POOLTAG_REGISTRY);
    if (Watcher == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    ExInitializePushLock(&Watcher->Lock);
    InitializeListHead(&Watcher->ClientList);

    Watcher->FallbackTimer =
        XdpTimerCreate(XdpRegWatcherTimeout, Watcher, DriverObject, DeviceObject);
    if (Watcher->FallbackTimer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlInitUnicodeString(&UnicodeName, KeyName);
    InitializeObjectAttributes(
        &ObjectAttributes, &UnicodeName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL, NULL);
    Status = ZwOpenKey(&Watcher->Key, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpRegWatcherRegister(Watcher);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Watcher != NULL) {
            XdpRegWatcherDelete(Watcher);
            Watcher = NULL;
        }
    }

    return Watcher;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpRegWatcherDelete(
    _In_ XDP_REG_WATCHER *Watcher
    )
{
    KEVENT DeletedEvent;
    BOOLEAN ShouldWait = FALSE;

    RtlAcquirePushLockExclusive(&Watcher->Lock);

    ASSERT(IsListEmpty(&Watcher->ClientList));

    Watcher->IsDeleting = TRUE;

    if (Watcher->IsRegistered) {
        KeInitializeEvent(&DeletedEvent, NotificationEvent, FALSE);
        Watcher->DeletedEvent = &DeletedEvent;
        ShouldWait = TRUE;
    }

    //
    // Closing the key will also trigger a final notification if currently
    // registered.
    //
    if (Watcher->Key != NULL) {
        ZwClose(Watcher->Key);
    }

    RtlReleasePushLockExclusive(&Watcher->Lock);

    if (ShouldWait) {
        //
        // Wait for the work queue item to finish.
        //
        KeWaitForSingleObject(&DeletedEvent, Executive, KernelMode, FALSE, NULL);
    }

    //
    // Cancel the fallback timer and wait for orderly shutdown.
    //
    if (Watcher->FallbackTimer != NULL) {
        XdpTimerShutdown(Watcher->FallbackTimer, TRUE, TRUE);
    }

    ExFreePoolWithTag(Watcher, XDP_POOLTAG_REGISTRY);
}
