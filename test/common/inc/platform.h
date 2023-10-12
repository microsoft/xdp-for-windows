//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Time unit conversion.
//
#define NS_TO_US(x)     ((x) / 1000)
#define US_TO_NS(x)     ((x) * 1000)
#define NS100_TO_US(x)  ((x) / 10)
#define US_TO_NS100(x)  ((x) * 10)
#define MS_TO_NS100(x)  ((x)*10000)
#define NS100_TO_MS(x)  ((x)/10000)
#define US_TO_MS(x)     ((x) / 1000)
#define MS_TO_US(x)     ((x) * 1000)
#define US_TO_S(x)      ((x) / (1000 * 1000))
#define S_TO_US(x)      ((x) * 1000 * 1000)
#define S_TO_NS(x)      ((x) * 1000 * 1000 * 1000)
#define MS_TO_S(x)      ((x) / 1000)
#define S_TO_MS(x)      ((x) * 1000)

typedef enum CXPLAT_THREAD_FLAGS {
    CXPLAT_THREAD_FLAG_NONE               = 0x0000,
    CXPLAT_THREAD_FLAG_SET_IDEAL_PROC     = 0x0001,
    CXPLAT_THREAD_FLAG_SET_AFFINITIZE     = 0x0002,
    CXPLAT_THREAD_FLAG_HIGH_PRIORITY      = 0x0004
} CXPLAT_THREAD_FLAGS;

#ifdef DEFINE_ENUM_FLAG_OPERATORS
DEFINE_ENUM_FLAG_OPERATORS(CXPLAT_THREAD_FLAGS);
#endif

//
// Static Analysis Interfaces
//

#if defined(_PREFAST_)
// _Analysis_assume_ will never result in any code generation for _exp,
// so using it will not have runtime impact, even if _exp has side effects.
#define CXPLAT_ANALYSIS_ASSUME(_exp) _Analysis_assume_(_exp)
#else // _PREFAST_
// CXPLAT_ANALYSIS_ASSUME ensures that _exp is parsed in non-analysis compile.
// On DEBUG, it's guaranteed to be parsed as part of the normal compile, but
// with non-DEBUG, use __noop to ensure _exp is parseable but without code
// generation.
#if DEBUG
#define CXPLAT_ANALYSIS_ASSUME(_exp) ((void) 0)
#else // DEBUG
#define CXPLAT_ANALYSIS_ASSUME(_exp) __noop(_exp)
#endif // DEBUG
#endif // _PREFAST_

#ifdef __clang__
#define CXPLAT_STATIC_ASSERT(X,Y) _Static_assert(X,Y)
#else
#define CXPLAT_STATIC_ASSERT(X,Y) static_assert(X,Y)
#endif

#define CXPLAT_ANALYSIS_ASSERT(X) __analysis_assert(X)

//
// Assertion Interfaces

#define CxPlatLogAssert(File, Line, Expr) ((void) 0)

#define CXPLAT_WIDE_STRING(_str) L##_str

#define CXPLAT_ASSERT_NOOP(_exp, _msg) \
    (CXPLAT_ANALYSIS_ASSUME(_exp), 0)

#define CXPLAT_ASSERT_LOG(_exp, _msg) \
    (CXPLAT_ANALYSIS_ASSUME(_exp), \
    ((!(_exp)) ? (CxPlatLogAssert(__FILE__, __LINE__, #_exp), FALSE) : TRUE))

#define CXPLAT_ASSERT_CRASH(_exp, _msg) \
    (CXPLAT_ANALYSIS_ASSUME(_exp), \
    ((!(_exp)) ? \
        (CxPlatLogAssert(__FILE__, __LINE__, #_exp), \
         __annotation(L"Debug", L"AssertFail", _msg), \
         DbgRaiseAssertionFailure(), FALSE) : \
        TRUE))

//
// Three types of asserts:
//
//  CXPLAT_DBG_ASSERT - Asserts that are too expensive to evaluate all the time.
//  CXPLAT_TEL_ASSERT - Asserts that are acceptable to always evaluate, but not
//                      always crash the system.
//  CXPLAT_FRE_ASSERT - Asserts that must always crash the system.
//

#if DEBUG
#define CXPLAT_DBG_ASSERT(_exp)          CXPLAT_ASSERT_CRASH(_exp, CXPLAT_WIDE_STRING(#_exp))
#define CXPLAT_DBG_ASSERTMSG(_exp, _msg) CXPLAT_ASSERT_CRASH(_exp, CXPLAT_WIDE_STRING(_msg))
#else
#define CXPLAT_DBG_ASSERT(_exp)          CXPLAT_ASSERT_NOOP(_exp, CXPLAT_WIDE_STRING(#_exp))
#define CXPLAT_DBG_ASSERTMSG(_exp, _msg) CXPLAT_ASSERT_NOOP(_exp, CXPLAT_WIDE_STRING(_msg))
#endif

#if DEBUG
#define CXPLAT_TEL_ASSERT(_exp)          CXPLAT_ASSERT_CRASH(_exp, CXPLAT_WIDE_STRING(#_exp))
#define CXPLAT_TEL_ASSERTMSG(_exp, _msg) CXPLAT_ASSERT_CRASH(_exp, CXPLAT_WIDE_STRING(_msg))
#define CXPLAT_TEL_ASSERTMSG_ARGS(_exp, _msg, _origin, _bucketArg1, _bucketArg2) \
                                         CXPLAT_ASSERT_CRASH(_exp, CXPLAT_WIDE_STRING(_msg))
#elif QUIC_TELEMETRY_ASSERTS
#define CXPLAT_TEL_ASSERT(_exp)          CXPLAT_ASSERT_LOG(_exp, CXPLAT_WIDE_STRING(#_exp))
#define CXPLAT_TEL_ASSERTMSG(_exp, _msg) CXPLAT_ASSERT_LOG(_exp, CXPLAT_WIDE_STRING(_msg))
#define CXPLAT_TEL_ASSERTMSG_ARGS(_exp, _msg, _origin, _bucketArg1, _bucketArg2) \
                                         CXPLAT_ASSERT_LOG(_exp, CXPLAT_WIDE_STRING(_msg))
#else
#define CXPLAT_TEL_ASSERT(_exp)          CXPLAT_ASSERT_NOOP(_exp, CXPLAT_WIDE_STRING(#_exp))
#define CXPLAT_TEL_ASSERTMSG(_exp, _msg) CXPLAT_ASSERT_NOOP(_exp, CXPLAT_WIDE_STRING(_msg))
#define CXPLAT_TEL_ASSERTMSG_ARGS(_exp, _msg, _origin, _bucketArg1, _bucketArg2) \
                                         CXPLAT_ASSERT_NOOP(_exp, CXPLAT_WIDE_STRING(_msg))
#endif

#define CXPLAT_FRE_ASSERT(_exp)          CXPLAT_ASSERT_CRASH(_exp, CXPLAT_WIDE_STRING(#_exp))
#define CXPLAT_FRE_ASSERTMSG(_exp, _msg) CXPLAT_ASSERT_CRASH(_exp, CXPLAT_WIDE_STRING(_msg))

#ifdef _KERNEL_MODE
#include "platform_kernel.h"
#else
#include "platform_user.h"
#endif
