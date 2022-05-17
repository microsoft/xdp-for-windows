//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Tracing Definitions:
//
// Control GUID:
// {D6143B5E-9FD6-44BA-BA02-FAD9EA0C263D}
//
#define WPP_CONTROL_GUIDS                           \
    WPP_DEFINE_CONTROL_GUID(                        \
        XdpMpTraceGuid,                             \
        (D6143B5E,9FD6,44BA,BA02,FAD9EA0C263D),     \
        WPP_DEFINE_BIT(TRACE_CONTROL)               \
        WPP_DEFINE_BIT(TRACE_DATAPATH)              \
        )

//
// The following system defined definitions may be used:
//
// TRACE_LEVEL_FATAL = 1        // Abnormal exit or termination.
// TRACE_LEVEL_ERROR = 2        // Severe errors that need logging.
// TRACE_LEVEL_WARNING = 3      // Warnings such as allocation failures.
// TRACE_LEVEL_INFORMATION = 4  // Including non-error cases.
// TRACE_LEVEL_VERBOSE = 5      // Detailed traces from intermediate steps.
//
// begin_wpp config
//
// USEPREFIX(TraceFatal,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceFatal{LEVEL=TRACE_LEVEL_FATAL}(FLAGS,MSG,...);
//
// USEPREFIX(TraceError,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceError{LEVEL=TRACE_LEVEL_ERROR}(FLAGS,MSG,...);
//
// USEPREFIX(TraceWarn,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceWarn{LEVEL=TRACE_LEVEL_WARNING}(FLAGS,MSG,...);
//
// USEPREFIX(TraceInfo,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceInfo{LEVEL=TRACE_LEVEL_INFORMATION}(FLAGS,MSG,...);
//
// USEPREFIX(TraceVerbose,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceVerbose{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS,MSG,...);
//
// USEPREFIX(TraceEnter,"%!STDPREFIX! %!FUNC!:%!LINE! --->%!SPACE!");
// FUNC TraceEnter{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS,MSG,...);
//
// USEPREFIX(TraceExitSuccess,"%!STDPREFIX! %!FUNC!:%!LINE! <---%!SPACE! ");
// FUNC TraceExitSuccess{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS,...);
// USESUFFIX (TraceExitSuccess, "STATUS_SUCCESS");
//
// USEPREFIX(TraceExitStatus,"%!STDPREFIX! %!FUNC!:%!LINE! <---%!SPACE!");
// FUNC TraceExitStatus{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS);
// USESUFFIX (TraceExitStatus, "%!STATUS!", Status);
//
// CUSTOM_TYPE(HOOK_LAYER, ItemEnum(_XDP_HOOK_LAYER));
// CUSTOM_TYPE(HOOK_DIR, ItemEnum(_XDP_HOOK_DATAPATH_DIRECTION));
// CUSTOM_TYPE(HOOK_SUBLAYER, ItemEnum(_XDP_HOOK_SUBLAYER));
// CUSTOM_TYPE(EXTENSION_TYPE, ItemEnum(_XDP_EXTENSION_TYPE));
//
// DEFINE_CPLX_TYPE(HEXDUMP, WPP_LOGHEXDUMP, WPP_HEXDUMP, ItemHEXDump, "s", _HEX_, 0, 2);
//
// end_wpp
//

#define WPP_LEVEL_FLAGS_ENABLED(LEVEL, FLAGS) \
    (WPP_LEVEL_ENABLED(FLAGS) && (WPP_CONTROL(WPP_BIT_ ## FLAGS).Level >= LEVEL))
#define WPP_LEVEL_FLAGS_LOGGER(LEVEL, FLAGS) WPP_LEVEL_LOGGER(FLAGS)

//
// Opt-in to a WPP recorder feature that enables independent evaluation of
// conditions to decide if a message needs to be sent to the recorder, an
// enabled session, or both.
//
#define ENABLE_WPP_TRACE_FILTERING_WITH_WPP_RECORDER 1

//
// Logger and Enabled macros that support custom recorders. They simply delegate
// to the default.
//
#define WPP_IFRLOG_LEVEL_FLAGS_ENABLED(IFRLOG, LEVEL, FLAGS) WPP_LEVEL_FLAGS_ENABLED(LEVEL, FLAGS)
#define WPP_IFRLOG_LEVEL_FLAGS_LOGGER(IFRLOG, LEVEL, FLAGS)  WPP_LEVEL_FLAGS_LOGGER(LEVEL, FLAGS)

#define WPP_LOGHEXDUMP(x) \
    WPP_LOGPAIR(sizeof(UINT16), &(x).Length) \
    WPP_LOGPAIR((x).Length, (x).Buffer)

typedef struct _WPP_HEXDUMP {
    CONST VOID *Buffer;
    UINT16 Length;
} WPP_HEXDUMP;

FORCEINLINE
WPP_HEXDUMP
WppHexDump(
    _In_ CONST VOID *Buffer,
    _In_ SIZE_T Length
    )
{
    WPP_HEXDUMP WppHexDump;

    WppHexDump.Buffer = Buffer;

    if (Buffer == NULL) {
        WppHexDump.Length = 0;
    } else  {
        WppHexDump.Length = (UINT16)min(Length, MAXUINT16);
    }

    return WppHexDump;
}
