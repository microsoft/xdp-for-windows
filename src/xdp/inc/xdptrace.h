//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <TraceLoggingProvider.h>
#include <evntrace.h>

//
// TraceLogging Provider GUID:
// {D6143B5C-9FD6-44BA-BA02-FAD9EA0C263D}
// (Reusing the same GUID as the previous WPP provider for compatibility)
//
TRACELOGGING_DECLARE_PROVIDER(XdpTraceProvider);

//
// Trace flags matching the original WPP flags
//
#define TRACE_CORE      0x0001
#define TRACE_XSK       0x0002
#define TRACE_GENERIC   0x0004
#define TRACE_NATIVE    0x0008
#define TRACE_RTL       0x0010
#define TRACE_LWF       0x0020

//
// TraceLogging macros that replace the WPP trace functions
// These accept variadic TraceLogging field arguments to log all parameters
//

#define TraceFatal(Flags, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "Fatal", \
        TraceLoggingLevel(WINEVENT_LEVEL_CRITICAL), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        __VA_ARGS__)

#define TraceError(Flags, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "Error", \
        TraceLoggingLevel(WINEVENT_LEVEL_ERROR), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        __VA_ARGS__)

#define TraceWarn(Flags, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "Warning", \
        TraceLoggingLevel(WINEVENT_LEVEL_WARNING), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        __VA_ARGS__)

#define TraceInfo(Flags, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "Information", \
        TraceLoggingLevel(WINEVENT_LEVEL_INFO), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        __VA_ARGS__)

#define TraceVerbose(Flags, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "Verbose", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        __VA_ARGS__)

#define TraceEnter(Flags, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "Enter", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        __VA_ARGS__)

//
// Special macros for function exit
//
#define TraceExitSuccess(Flags, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "ExitSuccess", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        TraceLoggingString("STATUS_SUCCESS", "Status"), \
        __VA_ARGS__)

#define TraceExitStatus(Flags, Status, ...) \
    TraceLoggingWrite(XdpTraceProvider, \
        "ExitStatus", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(Flags), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"), \
        TraceLoggingNTStatus(Status, "Status"), \
        __VA_ARGS__)

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

#define TraceLoggingIPv6Address(addr, name) \
    TraceLoggingBinary(addr, 16, name)

//
// Initialization and cleanup functions
//
NTSTATUS XdpTraceInitialize(VOID);
VOID XdpTraceCleanup(VOID);
