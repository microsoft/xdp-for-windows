/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    Kernel Mode Test Driver

--*/

#include <ntddk.h>
#include <wdf.h>

#include "fntrace.h"

#include "driver.tmh"

#ifndef KRTL_INIT_SEGMENT
#define KRTL_INIT_SEGMENT "INIT"
#endif
#ifndef KRTL_PAGE_SEGMENT
#define KRTL_PAGE_SEGMENT "PAGE"
#endif
#ifndef KRTL_NONPAGED_SEGMENT
#define KRTL_NONPAGED_SEGMENT ".text"
#endif

// Use on code in the INIT segment. (Code is discarded after DriverEntry returns.)
#define INITCODE __declspec(code_seg(KRTL_INIT_SEGMENT))

// Use on pageable functions.
#define PAGEDX __declspec(code_seg(KRTL_PAGE_SEGMENT))

#define TEST_DRV_POOL 'vDsT' // TsDv

EVT_WDF_DRIVER_UNLOAD TestDrvDriverUnload;
PDRIVER_OBJECT GlobalDriverObject;

_No_competing_thread_
INITCODE
NTSTATUS
TestDrvCtlInitialize(
    _In_ WDFDRIVER Driver
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
TestDrvCtlUninitialize(
    );

extern "C"
INITCODE
_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    A success status as determined by NT_SUCCESS macro, if successful.

--*/
{
    NTSTATUS Status;
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;

    GlobalDriverObject = DriverObject;

    WPP_INIT_TRACING(GlobalDriverObject, RegistryPath);

    //
    // Create the WdfDriver Object
    //
    WDF_DRIVER_CONFIG_INIT(&Config, NULL);
    Config.EvtDriverUnload = TestDrvDriverUnload;
    Config.DriverInitFlags = WdfDriverInitNonPnpDriver;
    Config.DriverPoolTag = TEST_DRV_POOL;

    Status =
        WdfDriverCreate(
            DriverObject,
            RegistryPath,
            WDF_NO_OBJECT_ATTRIBUTES,
            &Config,
            &Driver);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WdfDriverCreate failed");
        goto Error;
    }

    //
    // Initialize the device control interface.
    //
    Status = TestDrvCtlInitialize(Driver);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "TestDrvCtlInitialize failed");
        goto Error;
    }

    TraceInfo(
        "[test] Started");

Error:

    return Status;
}

_Function_class_(EVT_WDF_DRIVER_UNLOAD)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
void
TestDrvDriverUnload(
    _In_ WDFDRIVER Driver
    )
/*++

Routine Description:

    TestDrvDriverUnload will clean up any resources that were allocated for
    this driver.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

--*/
{
    UNREFERENCED_PARAMETER(Driver);
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    TestDrvCtlUninitialize();

    TraceInfo(
        "[test] Stopped");

    WPP_CLEANUP(GlobalDriverObject);
}
