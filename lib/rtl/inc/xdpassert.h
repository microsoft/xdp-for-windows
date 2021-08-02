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
#define ASSERT(e) NT_ASSERT(e)

#define FRE_ASSERT(e) \
    (NT_VERIFY(e) ? TRUE : (RtlFailFast(FAST_FAIL_INVALID_ARG), FALSE))
