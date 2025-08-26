//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Error logging and bail macros to ensure each failure path has a unique
// logging statement with proper attribution.
//

//
// LOG_AND_BAIL_ON_ERROR: Logs an error with unique attribution and jumps to Exit label
// Usage: LOG_AND_BAIL_ON_ERROR(StatusVar, ErrorCode, "Descriptive error message", ...);
//
#define LOG_AND_BAIL_ON_ERROR(StatusVar, ErrorCode, Format, ...) do { \
    TraceError(TRACE_CORE, __FUNCTION__ ":" STRINGIFY(__LINE__) ": " Format, ##__VA_ARGS__); \
    StatusVar = ErrorCode; \
    goto Exit; \
} while (0)

//
// LOG_AND_BAIL_ON_ERROR_IF: Conditionally logs error and bails if condition is true
// Usage: LOG_AND_BAIL_ON_ERROR_IF(condition, StatusVar, ErrorCode, "Error message", ...);
//
#define LOG_AND_BAIL_ON_ERROR_IF(Condition, StatusVar, ErrorCode, Format, ...) do { \
    if (Condition) { \
        TraceError(TRACE_CORE, __FUNCTION__ ":" STRINGIFY(__LINE__) ": " Format, ##__VA_ARGS__); \
        StatusVar = ErrorCode; \
        goto Exit; \
    } \
} while (0)

//
// LOG_AND_BAIL_ON_NTSTATUS: Logs NT status error and bails if status indicates failure
// Usage: LOG_AND_BAIL_ON_NTSTATUS(Status, "Operation description", ...);
//
#define LOG_AND_BAIL_ON_NTSTATUS(Status, Format, ...) do { \
    if (!NT_SUCCESS(Status)) { \
        TraceError(TRACE_CORE, __FUNCTION__ ":" STRINGIFY(__LINE__) ": " Format " Status=%!STATUS!", ##__VA_ARGS__, Status); \
        goto Exit; \
    } \
} while (0)

//
// LOG_AND_BAIL_ON_NULL: Logs error and bails if pointer is NULL
// Usage: LOG_AND_BAIL_ON_NULL(ptr, StatusVar, ErrorCode, "Allocation description", ...);
//
#define LOG_AND_BAIL_ON_NULL(Ptr, StatusVar, ErrorCode, Format, ...) do { \
    if ((Ptr) == NULL) { \
        TraceError(TRACE_CORE, __FUNCTION__ ":" STRINGIFY(__LINE__) ": " Format, ##__VA_ARGS__); \
        StatusVar = ErrorCode; \
        goto Exit; \
    } \
} while (0)

//
// LOG_AND_BAIL_ON_CONDITION: Generic conditional error logging and bail
// Usage: LOG_AND_BAIL_ON_CONDITION(condition, StatusVar, ErrorCode, "Error description", ...);
//
#define LOG_AND_BAIL_ON_CONDITION(Condition, StatusVar, ErrorCode, Format, ...) do { \
    if (Condition) { \
        TraceError(TRACE_CORE, __FUNCTION__ ":" STRINGIFY(__LINE__) ": " Format, ##__VA_ARGS__); \
        StatusVar = ErrorCode; \
        goto Exit; \
    } \
} while (0)

//
// STRINGIFY helper macro for converting line numbers to strings
//
#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)