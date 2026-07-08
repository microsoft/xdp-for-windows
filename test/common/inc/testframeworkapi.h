//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Test framework abstraction layer. This interface exists so that XDP tests may
// use different test frameworks without changing test logic.
//

#include <xdp/wincommon.h>

VOID
StopTest();

//
// Marks the current test as skipped for the given reason and stops executing
// the remainder of the test. Use via the TEST_SKIP macro.
//
VOID
SkipTest(
    _In_z_ PCWSTR Reason
    );

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
