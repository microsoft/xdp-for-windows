//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Provides assertion wrappers.
//

#pragma once

#ifdef ASSERT
#undef ASSERT
#endif

#ifdef KERNEL_MODE
#if DBG
// Ensure the system bugchecks if KD is disabled.
#define ASSERT(e) \
    ((NT_ASSERT_ASSUME(e)) ? \
        TRUE : (KD_DEBUGGER_ENABLED ? FALSE : (RtlFailFast(FAST_FAIL_INVALID_ARG), FALSE)))
#else
#define ASSERT(e) NT_ASSERT_ASSUME(e)
#endif
#else
#if __SANITIZE_ADDRESS__
// Instead of asserting (which simply exits the process) generate an AV that ASAN will catch.
#define ASSERT_ACTION ((*(UINT16 *)0xDEAD) = 0xDEAD)
#endif
#if DBG
#ifndef ASSERT_ACTION
#define ASSERT_ACTION DbgRaiseAssertionFailure()
#endif
#define ASSERT(e) ((e) ? TRUE : (ASSERT_ACTION, FALSE))
#elif defined(_PREFAST_)
#define ASSERT(e) _Analysis_assume_(e)
#else
#define ASSERT(e)
#endif
#endif

#ifdef KERNEL_MODE
#define FRE_ASSERT(e) \
    (NT_VERIFY(e) ? TRUE : (RtlFailFast(FAST_FAIL_INVALID_ARG), FALSE))
#elif DBG
#define FRE_ASSERT(e) (ASSERT(e) ? TRUE : (__fastfail(FAST_FAIL_INVALID_ARG), FALSE))
#else
#define FRE_ASSERT(e) ((e) ? TRUE : (__fastfail(FAST_FAIL_INVALID_ARG), FALSE))
#endif
