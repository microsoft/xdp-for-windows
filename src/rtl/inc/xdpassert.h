//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Provides assertion wrappers.
//

#pragma once

#include <xdp/details/assert.h>

#ifdef ASSERT
#undef ASSERT
#endif

#define ASSERT XDP_ASSERT
#define FRE_ASSERT XDP_FRE_ASSERT
