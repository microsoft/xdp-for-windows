//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#ifdef XDP_BUILD_VERSION
#define _XDP_BUILD_VERSION "-" STR(XDP_BUILD_VERSION)
#define XDP_OFFICIAL_BUILD 1
#else
#define _XDP_BUILD_VERSION ""
#endif

#ifdef XDP_OFFICIAL_BUILD
#define _XDP_BUILD_TYPE "official"
#else
#define _XDP_BUILD_TYPE "private"
#endif

#define XDP_VERSION_STR \
    STR(XDP_MAJOR_VERSION) "." STR(XDP_MINOR_VERSION) "." STR(XDP_PATCH_VERSION) \
    "+" STR(XDP_COMMIT_VERSION) _XDP_BUILD_VERSION "-" _XDP_BUILD_TYPE "\0"
