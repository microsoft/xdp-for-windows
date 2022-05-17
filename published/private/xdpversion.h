//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#ifdef XDP_BUILD_VERSION
#define _XDP_BUILD_VERSION "-" STR(XDP_BUILD_VERSION)
#else
#define _XDP_BUILD_VERSION ""
#endif

#define XDP_VERSION_STR \
    STR(XDP_MAJOR_VERSION) "." STR(XDP_MINOR_VERSION) "." STR(XDP_PATCH_VERSION) \
    "+" STR(XDP_COMMIT_VERSION) _XDP_BUILD_VERSION "-" STR(XDP_BUILD_TYPE) "\0"
