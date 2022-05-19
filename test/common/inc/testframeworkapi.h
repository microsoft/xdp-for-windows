//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Test framework abstraction layer. This interface exists so that XDP tests may
// use different test frameworks without changing test logic.
//

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

VOID
StopTest();

VOID
LogTestFailure(
    _In_z_ const LPWSTR File,
    _In_z_ const LPWSTR Function,
    INT Line,
    _Printf_format_string_ const LPWSTR Format,
    ...
    );

VOID
LogTestWarning(
    _In_z_ const LPWSTR File,
    _In_z_ const LPWSTR Function,
    INT Line,
    _Printf_format_string_ const LPWSTR Format,
    ...
    );

#ifdef __cplusplus
} /* extern "C" */
#endif
