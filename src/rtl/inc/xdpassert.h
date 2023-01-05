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
#define ASSERT(e) NT_ASSERT_ASSUME(e)
#else
#if DBG
#define ASSERT(e) ((e) ? TRUE : (DbgRaiseAssertionFailure(), FALSE))
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
