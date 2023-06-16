//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This is a layer of indirection to not call framework-dependent functions from test collateral.
//

#include <winerror.h>
#include "testframeworkapi.h"

#define TEST_FAILURE(Format, ...) { \
    LogTestFailure(L"" __FILE__, L"" __FUNCTION__, __LINE__, L"" Format, ##__VA_ARGS__); \
    StopTest(); \
}

#define TEST_WARNING(Format, ...) LogTestWarning(L"" __FILE__, L"" __FUNCTION__, __LINE__, L"" Format, ##__VA_ARGS__)

#define TEST_EQUAL(expected, condition) { \
    if ((condition) != (expected)) \
    { \
        TEST_FAILURE(#condition " not equal to " #expected); \
    } \
}

#define TEST_NOT_EQUAL(expected, condition) { \
    if ((condition) == (expected)) { \
        TEST_FAILURE(#condition " equals " #expected); \
    } \
}

#define TEST_TRUE(condition) { \
    if (!(condition)) { \
        TEST_FAILURE(#condition " not true"); \
    } \
}

#define TEST_FALSE(condition) { \
    if (condition) { \
        TEST_FAILURE(#condition " not false"); \
    } \
}

#define TEST_NOT_NULL(condition) { \
    if ((condition) == NULL) { \
        TEST_FAILURE(#condition " is NULL"); \
    } \
}

#define TEST_HRESULT(condition) { \
    HRESULT hr_ = (condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
    } \
}

#define TEST_NTSTATUS(condition) { \
    HRESULT hr_ = HRESULT_FROM_NT(condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
    } \
}
