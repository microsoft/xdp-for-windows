//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ntdef.h>
#include <ntstatus.h>
#include <ntifs.h>
#include <ntintsafe.h>

#define RTL_IS_POWER_OF_TWO(Value) \
    ((Value != 0) && !((Value) & ((Value) - 1)))

// Copied from zwapi.h
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSYSAPI
NTSTATUS
NTAPI
ZwNotifyChangeKey(
    _In_ HANDLE KeyHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG CompletionFilter,
    _In_ BOOLEAN WatchTree,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ BOOLEAN Asynchronous
    );

#include <xdpassert.h>
#include <xdplifetime.h>
#include <xdprefcount.h>
#include <xdpregistry.h>
#include <xdprtl.h>
#include <xdptimer.h>
#include <xdptrace.h>
#include <xdpworkqueue.h>

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#define XDP_POOLTAG_LIFETIME    'LcdX' // XdcL
#define XDP_POOLTAG_REGISTRY    'RcdX' // XdcR
#define XDP_POOLTAG_TIMER       'TcdX' // XdcT
#define XDP_POOLTAG_WORKQUEUE   'WcdX' // XdcW

extern EX_RUNDOWN_REF XdpRtlRundown;
