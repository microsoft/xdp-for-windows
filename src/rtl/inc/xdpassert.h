//
// Copyright (C) Microsoft Corporation. All rights reserved.
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

#define FRE_ASSERT(e) \
    (NT_VERIFY(e) ? TRUE : (RtlFailFast(FAST_FAIL_INVALID_ARG), FALSE))
