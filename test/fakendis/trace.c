//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// TraceLogging provider implementation for FakeNDIS
//

#include "precomp.h"

//
// Define the TraceLogging provider
//
TRACELOGGING_DEFINE_PROVIDER(
    FndisTraceProvider,
    "Microsoft-FakeNDIS-TraceLogging",
    // Provider GUID: {D6143B60-9FD6-44BA-BA02-FAD9EA0C263D}
    (0xD6143B60, 0x9FD6, 0x44BA, 0xBA, 0x02, 0xFA, 0xD9, 0xEA, 0x0C, 0x26, 0x3D));

NTSTATUS
FndisTraceInitialize(
    VOID
    )
{
    NTSTATUS Status;

    Status = TraceLoggingRegister(FndisTraceProvider);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    return STATUS_SUCCESS;
}

VOID
FndisTraceCleanup(
    VOID
    )
{
    TraceLoggingUnregister(FndisTraceProvider);
}