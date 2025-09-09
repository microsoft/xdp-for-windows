//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <TraceLoggingProvider.h>
#include <evntrace.h>

//
// TraceLogging Provider GUID:
// {D6143B5D-9FD6-44BA-BA02-FAD9EA0C263D}
// (Using the same GUID as the previous WPP provider for compatibility)
//
TRACELOGGING_DECLARE_PROVIDER(SpinXskTraceProvider);

//
// Trace flags matching the original WPP flags
//
#define TRACE_SPINXSK  0x0001

//
// TraceLogging macros that replace the WPP trace functions
// These maintain the same interface as the original WPP macros
//

#define TraceFatal(...) \
    TraceLoggingWrite(SpinXskTraceProvider, \
        "Fatal", \
        TraceLoggingLevel(WINEVENT_LEVEL_CRITICAL), \
        TraceLoggingKeyword(TRACE_SPINXSK), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceError(...) \
    TraceLoggingWrite(SpinXskTraceProvider, \
        "Error", \
        TraceLoggingLevel(WINEVENT_LEVEL_ERROR), \
        TraceLoggingKeyword(TRACE_SPINXSK), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceWarn(...) \
    TraceLoggingWrite(SpinXskTraceProvider, \
        "Warning", \
        TraceLoggingLevel(WINEVENT_LEVEL_WARNING), \
        TraceLoggingKeyword(TRACE_SPINXSK), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceInfo(...) \
    TraceLoggingWrite(SpinXskTraceProvider, \
        "Information", \
        TraceLoggingLevel(WINEVENT_LEVEL_INFO), \
        TraceLoggingKeyword(TRACE_SPINXSK), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceVerbose(...) \
    TraceLoggingWrite(SpinXskTraceProvider, \
        "Verbose", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(TRACE_SPINXSK), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceEnter(...) \
    TraceLoggingWrite(SpinXskTraceProvider, \
        "Enter", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(TRACE_SPINXSK), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceExit(...) \
    TraceLoggingWrite(SpinXskTraceProvider, \
        "Exit", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(TRACE_SPINXSK), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

//
// Initialization and cleanup functions (for user-mode applications these may be no-ops)
//
NTSTATUS SpinXskTraceInitialize(VOID);
VOID SpinXskTraceCleanup(VOID);
