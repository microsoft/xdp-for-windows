//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// The XDP work queue is a derivative of the netio work queue. The main
// difference is XDP work queues can be deleted without waiting for the work
// queue to drain.
//

#pragma once

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
XDP_WORK_QUEUE_ROUTINE(
    _In_ SINGLE_LIST_ENTRY *WorkQueueHead
    );

typedef struct _XDP_WORK_QUEUE XDP_WORK_QUEUE;

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_WORK_QUEUE *
XdpCreateWorkQueue(
    _In_ XDP_WORK_QUEUE_ROUTINE *WorkQueueRoutine,
    _In_ KIRQL MaxIrql,
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    );

_When_(Wait == FALSE, _IRQL_requires_max_(DISPATCH_LEVEL))
_When_(Wait != FALSE, _IRQL_requires_(PASSIVE_LEVEL))
VOID
XdpShutdownWorkQueue(
    _In_ XDP_WORK_QUEUE *WorkQueue,
    _In_ BOOLEAN Wait
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpSetWorkQueuePriority(
    _In_ XDP_WORK_QUEUE *WorkQueue,
    _In_ WORK_QUEUE_TYPE Priority
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpInsertWorkQueue(
    _In_ XDP_WORK_QUEUE *WorkQueue,
    _In_ SINGLE_LIST_ENTRY *WorkQueueEntry
    );
