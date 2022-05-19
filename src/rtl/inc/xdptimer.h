//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// A simple passive-level timer.
//

typedef struct _XDP_TIMER XDP_TIMER;

_IRQL_requires_max_(PASSIVE_LEVEL)
XDP_TIMER *
XdpTimerCreate(
    _In_ WORKER_THREAD_ROUTINE *TimerRoutine,
    _In_opt_ VOID *TimerContext,
    _In_opt_ DRIVER_OBJECT *DriverObject,
    _In_opt_ DEVICE_OBJECT *DeviceObject
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
XdpTimerStart(
    _In_ XDP_TIMER *Timer,
    _In_ UINT32 DueTimeInMs,
    _Out_opt_ BOOLEAN *Started
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
XdpTimerCancel(
    _In_ XDP_TIMER *Timer
    );

_IRQL_requires_(PASSIVE_LEVEL)
BOOLEAN
XdpTimerShutdown(
    _In_ XDP_TIMER *Timer,
    _In_ BOOLEAN Cancel,
    _In_ BOOLEAN Wait
    );
