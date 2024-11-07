//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_ASSERT_H
#define XDP_DETAILS_ASSERT_H

//
// This file contains assertion macros. These should not be invoked directly, and their
// behavior is subject to change.
//

#ifdef KERNEL_MODE
#if DBG
// Ensure the system bugchecks if KD is disabled.
#define XDP_ASSERT(e) \
    ((NT_ASSERT_ASSUME(e)) ? \
        TRUE : (KD_DEBUGGER_ENABLED ? FALSE : (RtlFailFast(FAST_FAIL_INVALID_ARG), FALSE)))
#else
#define XDP_ASSERT(e) NT_ASSERT_ASSUME(e)
#endif
#else
#if __SANITIZE_ADDRESS__
// Instead of asserting (which simply exits the process) generate an AV that ASAN will catch.
#define XDP_ASSERT_ACTION ((*(UINT16 *)0xDEAD) = 0xDEAD)
#endif
#if DBG
#ifndef XDP_ASSERT_ACTION
#define XDP_ASSERT_ACTION DbgRaiseAssertionFailure()
#endif
#define XDP_ASSERT(e) ((e) ? TRUE : (XDP_ASSERT_ACTION, FALSE))
#elif defined(_PREFAST_)
#define XDP_ASSERT(e) _Analysis_assume_(e)
#else
#define XDP_ASSERT(e)
#endif
#endif

#ifdef KERNEL_MODE
#define XDP_FRE_ASSERT(e) \
    (NT_VERIFY(e) ? TRUE : (RtlFailFast(FAST_FAIL_INVALID_ARG), FALSE))
#elif DBG
#define XDP_FRE_ASSERT(e) (ASSERT(e) ? TRUE : (__fastfail(FAST_FAIL_INVALID_ARG), FALSE))
#else
#define XDP_FRE_ASSERT(e) ((e) ? TRUE : (__fastfail(FAST_FAIL_INVALID_ARG), FALSE))
#endif

#endif
