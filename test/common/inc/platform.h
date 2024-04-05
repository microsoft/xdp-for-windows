//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This API is comprised of copied snippets of the cross-platform pieces of
// https://github.com/microsoft/msquic.
// Eventually this API should be replaced with https://github.com/microsoft/cxplat.
//

#pragma once

EXTERN_C_START

//
// Status type and macros.
//

#if defined(KERNEL_MODE)
#define CXPLAT_STATUS NTSTATUS
#define CXPLAT_FAILED(X) !NT_SUCCESS(X)
#define CXPLAT_SUCCEEDED(X) NT_SUCCESS(X)
#define CXPLAT_STATUS_SUCCESS STATUS_SUCCESS
#define CXPLAT_STATUS_FAIL STATUS_UNSUCCESSFUL
#else
#define CXPLAT_STATUS HRESULT
#define CXPLAT_FAILED(X) FAILED(X)
#define CXPLAT_SUCCEEDED(X) SUCCEEDED(X)
#define CXPLAT_STATUS_SUCCESS S_OK
#define CXPLAT_STATUS_FAIL E_FAIL
#endif

//
// Code segment declarations.
//

#if defined(KERNEL_MODE)
#define CODE_SEG(segment) __declspec(code_seg(segment))
#else
#define CODE_SEG(segment)
#endif

#ifndef KRTL_INIT_SEGMENT
#define KRTL_INIT_SEGMENT "INIT"
#endif
#ifndef KRTL_PAGE_SEGMENT
#define KRTL_PAGE_SEGMENT "PAGE"
#endif
#ifndef KRTL_NONPAGED_SEGMENT
#define KRTL_NONPAGED_SEGMENT ".text"
#endif

// Use on pageable functions.
#define PAGED CODE_SEG(KRTL_PAGE_SEGMENT) _IRQL_always_function_max_(PASSIVE_LEVEL)

// Use on pageable functions, where you don't want the SAL IRQL annotation to say PASSIVE_LEVEL.
#define PAGEDX CODE_SEG(KRTL_PAGE_SEGMENT)

// Use on code in the INIT segment.  (Code is discarded after DriverEntry returns.)
#define INITCODE CODE_SEG(KRTL_INIT_SEGMENT)

// Use on code that must always be locked in memory.
#define NONPAGED CODE_SEG(KRTL_NONPAGED_SEGMENT) _IRQL_requires_max_(DISPATCH_LEVEL)

// Use on code that must always be locked in memory, where you don't want SAL IRQL annotations.
#define NONPAGEDX CODE_SEG(KRTL_NONPAGED_SEGMENT)

#ifndef _KERNEL_MODE

#ifndef PAGED_CODE
#define PAGED_CODE() (void)0
#endif // PAGED_CODE

#ifndef INIT_CODE
#define INIT_CODE() (void)0
#endif // INIT_CODE

#endif // _KERNEL_MODE

// Use on classes or structs.  Class member functions & compiler-generated code
// will default to the PAGE segment.  You can override any member function with `NONPAGED`.
#define KRTL_CLASS CODE_SEG(KRTL_PAGE_SEGMENT) __declspec(empty_bases)

// Use on classes or structs.  Class member functions & compiler-generated code
// will default to the NONPAGED segment.  You can override any member function with `PAGED`.
#define KRTL_CLASS_DPC_ALLOC __declspec(empty_bases)

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

//
// Create Thread Interfaces
//

DECLARE_HANDLE(CXPLAT_THREAD);

#if defined(KERNEL_MODE)
typedef VOID CXPLAT_THREAD_RETURN_TYPE;
#define CXPLAT_THREAD_RETURN(Status) PsTerminateSystemThread(Status)
#else
typedef DWORD CXPLAT_THREAD_RETURN_TYPE;
#define CXPLAT_THREAD_RETURN(Status) return (DWORD)(Status)
#endif

typedef
_IRQL_requires_same_
CXPLAT_THREAD_RETURN_TYPE
CXPLAT_THREAD_ROUTINE(
    _In_ VOID* Context
    );

typedef enum CXPLAT_THREAD_FLAGS {
    CXPLAT_THREAD_FLAG_NONE               = 0x0000,
    CXPLAT_THREAD_FLAG_SET_IDEAL_PROC     = 0x0001,
    CXPLAT_THREAD_FLAG_SET_AFFINITIZE     = 0x0002,
    CXPLAT_THREAD_FLAG_HIGH_PRIORITY      = 0x0004
} CXPLAT_THREAD_FLAGS;

#ifdef DEFINE_ENUM_FLAG_OPERATORS
DEFINE_ENUM_FLAG_OPERATORS(CXPLAT_THREAD_FLAGS);
#endif

typedef struct CXPLAT_THREAD_CONFIG {
    UINT16 Flags;
    UINT16 IdealProcessor;
    _Field_z_ const CHAR* Name;
    CXPLAT_THREAD_ROUTINE* Callback;
    VOID* Context;
} CXPLAT_THREAD_CONFIG;

CXPLAT_STATUS
CxPlatThreadCreate(
    _In_ CXPLAT_THREAD_CONFIG* Config,
    _Out_ CXPLAT_THREAD* Thread
    );

VOID
CxPlatThreadDelete(
    _In_ CXPLAT_THREAD
    );

BOOLEAN
CxPlatThreadWaitForever(
    _In_ CXPLAT_THREAD
    );

BOOLEAN
CxPlatThreadWait(
    _In_ CXPLAT_THREAD,
    _In_ UINT32 TimeoutMs
    );


//
// Crypto Interfaces
//

//
// Returns cryptographically random bytes.
//
_IRQL_requires_max_(DISPATCH_LEVEL)
CXPLAT_STATUS
CxPlatRandom(
    _In_ UINT32 BufferLen,
    _Out_writes_bytes_(BufferLen) void* Buffer
    );

//
// Initializes the PAL library. Calls to this and
// CxPlatformUninitialize must be serialized and cannot overlap.
//
PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    void
    );

//
// Uninitializes the PAL library. Calls to this and
// CxPlatformInitialize must be serialized and cannot overlap.
//
PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    );

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
#elif CXPLAT_TELEMETRY_ASSERTS
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

EXTERN_C_END

#ifdef _KERNEL_MODE
#include "platform_kernel.h"
#else
#include "platform_user.h"
#endif
