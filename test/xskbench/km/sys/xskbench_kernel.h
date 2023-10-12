//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <xdpapi.h>

#define printf(format, ...) \
    KmPlatPrint(__FUNCTION__, __LINE__, 4, format, __VA_ARGS__)

#define printf_error(format, ...) \
    KmPlatPrint(__FUNCTION__, __LINE__, 3, format, __VA_ARGS__)

#define printf_verbose(format, ...) \
    if (verbose) KmPlatPrint(__FUNCTION__, __LINE__, 5, format, __VA_ARGS__)

#define ABORT(...) \
    printf_error(__VA_ARGS__); KeBugCheck(0xeeeeeeee)

#define ASSERT_FRE(expr) \
    if (!(expr)) { ABORT("(%s) failed line %d\n", #expr, __LINE__);}

XDP_API_PROVIDER_BINDING_CONTEXT *
CxPlatXdpApiGetProviderBindingContext(
    VOID
    );

VOID
CxPlatXdpApiInitialize(
    VOID
    );

VOID
CxPlatXdpApiUninitialize(
    VOID
    );

VOID
KmPlatPrint(
    char* Function,
    int Line,
    char level,
    const char* format,
    ...
    );
