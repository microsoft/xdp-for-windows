//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <TraceLoggingProvider.h>
#include <evntrace.h>

//
// TraceLogging Provider GUID:
// {D6143B62-9FD6-44BA-BA02-FAD9EA0C263D}
// (Using the same GUID as the previous WPP provider for compatibility)
//
TRACELOGGING_DECLARE_PROVIDER(FunctionalTestTraceProvider);

//
// Trace flags matching the original WPP flags
//
#define TRACE_FUNCTIONAL  0x0001

//
// TraceLogging macros that replace the WPP trace functions
// These maintain the same interface as the original WPP macros
//

#define TraceFatal(...) \
    TraceLoggingWrite(FunctionalTestTraceProvider, \
        "Fatal", \
        TraceLoggingLevel(WINEVENT_LEVEL_CRITICAL), \
        TraceLoggingKeyword(TRACE_FUNCTIONAL), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceError(...) \
    TraceLoggingWrite(FunctionalTestTraceProvider, \
        "Error", \
        TraceLoggingLevel(WINEVENT_LEVEL_ERROR), \
        TraceLoggingKeyword(TRACE_FUNCTIONAL), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceWarn(...) \
    TraceLoggingWrite(FunctionalTestTraceProvider, \
        "Warning", \
        TraceLoggingLevel(WINEVENT_LEVEL_WARNING), \
        TraceLoggingKeyword(TRACE_FUNCTIONAL), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceInfo(...) \
    TraceLoggingWrite(FunctionalTestTraceProvider, \
        "Information", \
        TraceLoggingLevel(WINEVENT_LEVEL_INFO), \
        TraceLoggingKeyword(TRACE_FUNCTIONAL), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceVerbose(...) \
    TraceLoggingWrite(FunctionalTestTraceProvider, \
        "Verbose", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(TRACE_FUNCTIONAL), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceEnter(...) \
    TraceLoggingWrite(FunctionalTestTraceProvider, \
        "Enter", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(TRACE_FUNCTIONAL), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

#define TraceExit(...) \
    TraceLoggingWrite(FunctionalTestTraceProvider, \
        "Exit", \
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), \
        TraceLoggingKeyword(TRACE_FUNCTIONAL), \
        TraceLoggingString(__FUNCTION__, "Function"), \
        TraceLoggingUInt32(__LINE__, "Line"))

//
// Initialization and cleanup functions
//
NTSTATUS FunctionalTestTraceInitialize(VOID);
VOID FunctionalTestTraceCleanup(VOID);
