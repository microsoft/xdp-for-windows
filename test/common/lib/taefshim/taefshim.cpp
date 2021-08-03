//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#define INLINE_TEST_METHOD_MARKUP
#include <WexTestClass.h>

#include "testframeworkapi.h"

VOID
StopTest()
{
    VERIFY_FAIL(L"Stop test execution.");
}

VOID
LogTestFailure(
    _In_z_ const LPWSTR File,
    _In_z_ const LPWSTR Function,
    INT Line,
    _Printf_format_string_ const LPWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    WEX::Logging::Log::Error(Buffer, File, Function, Line);
}

VOID
LogTestWarning(
    _In_z_ const LPWSTR File,
    _In_z_ const LPWSTR Function,
    INT Line,
    _Printf_format_string_ const LPWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    WEX::Logging::Log::Warning(Buffer, File, Function, Line);
}
