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

VOID
StopTest();

VOID
LogTestFailure(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    );

VOID
LogTestWarning(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    );
