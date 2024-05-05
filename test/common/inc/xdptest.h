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

#define TEST_FAILURE(Format, ...) LogTestFailure(L"" __FILE__, L"" __FUNCTION__, __LINE__, L"" Format, ##__VA_ARGS__)

#define TEST_WARNING(Format, ...) LogTestWarning(L"" __FILE__, L"" __FUNCTION__, __LINE__, L"" Format, ##__VA_ARGS__)

#define TEST_EQUAL(expected, condition) { \
    if ((condition) != (expected)) { \
        TEST_FAILURE(#condition " not equal to " #expected); \
        return; \
    } \
}

#define TEST_NOT_EQUAL(expected, condition) { \
    if ((condition) == (expected)) { \
        TEST_FAILURE(#condition " equals " #expected); \
        return; \
    } \
}

#define TEST_TRUE(condition) { \
    if (!(condition)) { \
        TEST_FAILURE(#condition " not true"); \
        return; \
    } \
}

#define TEST_FALSE(condition) { \
    if (condition) { \
        TEST_FAILURE(#condition " not false"); \
        return; \
    } \
}

#define TEST_NOT_NULL(condition) { \
    if ((condition) == NULL) { \
        TEST_FAILURE(#condition " is NULL"); \
        return; \
    } \
}

#define TEST_HRESULT(condition) { \
    HRESULT hr_ = (condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
        return; \
    } \
}

#define TEST_NTSTATUS(condition) { \
    HRESULT hr_ = HRESULT_FROM_NT(condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
        return; \
    } \
}

//
// goto variants
//

#define TEST_EQUAL_GOTO(expected, condition, label) { \
    if ((condition) != (expected)) { \
        TEST_FAILURE(#condition " not equal to " #expected); \
        goto label; \
    } \
}

#define TEST_NOT_EQUAL_GOTO(expected, condition, label) { \
    if ((condition) == (expected)) { \
        TEST_FAILURE(#condition " equals " #expected); \
        goto label; \
    } \
}

#define TEST_TRUE_GOTO(condition, label) { \
    if (!(condition)) { \
        TEST_FAILURE(#condition " not true"); \
        goto label; \
    } \
}

#define TEST_FALSE_GOTO(condition, label) { \
    if (condition) { \
        TEST_FAILURE(#condition " not false"); \
        goto label; \
    } \
}

#define TEST_NOT_NULL_GOTO(condition, label) { \
    if ((condition) == NULL) { \
        TEST_FAILURE(#condition " is NULL"); \
        goto label; \
    } \
}

#define TEST_HRESULT_GOTO(condition, label) { \
    HRESULT hr_ = (condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
        goto label; \
    } \
}

#define TEST_NTSTATUS_GOTO(condition, label) { \
    HRESULT hr_ = HRESULT_FROM_NT(condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
        goto label; \
    } \
}

//
// return variants
//

#define TEST_EQUAL_RET(expected, condition, retval) { \
    if ((condition) != (expected)) { \
        TEST_FAILURE(#condition " not equal to " #expected); \
        return retval; \
    } \
}

#define TEST_NOT_EQUAL_RET(expected, condition, retval) { \
    if ((condition) == (expected)) { \
        TEST_FAILURE(#condition " equals " #expected); \
        return retval; \
    } \
}

#define TEST_TRUE_RET(condition, retval) { \
    if (!(condition)) { \
        TEST_FAILURE(#condition " not true"); \
        return retval; \
    } \
}

#define TEST_FALSE_RET(condition, retval) { \
    if (condition) { \
        TEST_FAILURE(#condition " not false"); \
        return retval; \
    } \
}

#define TEST_NOT_NULL_RET(condition, retval) { \
    if ((condition) == NULL) { \
        TEST_FAILURE(#condition " is NULL"); \
        return retval; \
    } \
}

#define TEST_HRESULT_RET(condition, retval) { \
    HRESULT hr_ = (condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
        return retval; \
    } \
}

#define TEST_NTSTATUS_RET(condition, retval) { \
    HRESULT hr_ = HRESULT_FROM_NT(condition); \
    if (FAILED(hr_)) { \
        TEST_FAILURE(#condition " failed, 0x%x", hr_); \
        return retval; \
    } \
}
