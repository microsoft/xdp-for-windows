//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define printf_error(...) \
    fprintf(stderr, __VA_ARGS__)

#define printf_verbose(format, ...) \
    if (verbose) { LARGE_INTEGER Qpc; QueryPerformanceCounter(&Qpc); printf("Qpc=%llu " format, Qpc.QuadPart, __VA_ARGS__); }

#define ABORT(...) \
    printf_error(__VA_ARGS__); exit(1)

#define ASSERT_FRE(expr) \
    if (!(expr)) { ABORT("(%s) failed line %d\n", #expr, __LINE__);}

#define ASSERT(expr) \
    assert(expr)

#define ALIGN_DOWN_BY(length, alignment) \
    ((ULONG_PTR)(length)& ~(alignment - 1))
#define ALIGN_UP_BY(length, alignment) \
    (ALIGN_DOWN_BY(((ULONG_PTR)(length)+alignment - 1), alignment))

VOID
CxPlatXdpApiInitialize(
    VOID
    );

VOID
CxPlatXdpApiUninitialize(
    VOID
    );
