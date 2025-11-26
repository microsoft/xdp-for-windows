//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <TraceLoggingProvider.h>
#include <evntrace.h>

//
// TraceLogging Provider GUID:
// {D6143B60-9FD6-44BA-BA02-FAD9EA0C263D}
// (Using the same GUID as the previous WPP provider for compatibility)
//
TRACELOGGING_DECLARE_PROVIDER(FndisTraceProvider);

//
// Trace flags matching the original WPP flags
//
#define TRACE_CONTROL   0x0001
#define TRACE_DATAPATH  0x0002

//
// TraceLogging macros that replace the WPP trace functions
// These maintain the same interface as the original WPP macros
//

#define TraceFatal(Flags, ...) \
    TraceLoggingWrite(FndisTraceProvider, \
        "Fatal", \
        TraceLoggingLevel(WINEVENT_LEVEL_CRITICAL), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceError(Flags, ...) \
    TraceLoggingWrite(FndisTraceProvider, \
        "Error", \
        TraceLoggingLevel(WINEVENT_LEVEL_ERROR), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceWarn(Flags, ...) \
    TraceLoggingWrite(FndisTraceProvider, \
        "Warning", \
        TraceLoggingLevel(WINEVENT_LEVEL_WARNING), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceInfo(Flags, ...) \
    TraceLoggingWrite(FndisTraceProvider, \
        "Information", \
        TraceLoggingLevel(WINEVENT_LEVEL_INFO), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceVerbose(Flags, ...) \
    TraceLoggingWrite(FndisTraceProvider, \
        "Verbose", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceEnter(Flags, ...) \
    TraceLoggingWrite(FndisTraceProvider, \
        "Enter", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceExitSuccess(Flags) \
    TraceLoggingWrite(FndisTraceProvider, \
        "ExitSuccess", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        TraceLoggingString("STATUS_SUCCESS", "Status"))

#define TraceExitStatus(Flags, Status) \
    TraceLoggingWrite(FndisTraceProvider, \
        "ExitStatus", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        TraceLoggingNTStatus(Status, "Status"))

//
// Helper functions for complex data types
//
typedef struct _TRACE_HEXDUMP {
    const VOID *Buffer;
    UINT16 Length;
} TRACE_HEXDUMP;

FORCEINLINE
TRACE_HEXDUMP
TraceHexDump(
    _In_ const VOID *Buffer,
    _In_ SIZE_T Length
    )
{
    TRACE_HEXDUMP TraceHexDump;

    TraceHexDump.Buffer = Buffer;

    if (Buffer == NULL) {
        TraceHexDump.Length = 0;
    } else  {
        TraceHexDump.Length = (UINT16)min(Length, MAXUINT16);
    }

    return TraceHexDump;
}

//
// Helper macros for logging complex data
//
#define TraceLoggingHexDump(data, name) \
    TraceLoggingBinary((data).Buffer, (data).Length, name)

//
// Initialization and cleanup functions
//
NTSTATUS FndisTraceInitialize(VOID);
VOID FndisTraceCleanup(VOID);
