# LOG_AND_BAIL_ON_ERROR Macros

This document describes the LOG_AND_BAIL_ON_ERROR macro family added to ensure each failure path has a unique logging statement with proper attribution.

## Overview

The LOG_AND_BAIL_ON_ERROR macros combine error logging with error handling (goto Exit patterns) to ensure that every failure path in the code can be uniquely identified through logging. Each macro automatically captures the function name and line number, providing precise attribution for debugging.

## Available Macros

### LOG_AND_BAIL_ON_ERROR
Logs an error with unique attribution and jumps to the Exit label.

**Usage:**
```c
LOG_AND_BAIL_ON_ERROR(StatusVar, ErrorCode, "Descriptive error message", ...);
```

**Example:**
```c
if (some_condition_failed) {
    LOG_AND_BAIL_ON_ERROR(Status, STATUS_INVALID_PARAMETER, "Validation failed for parameter %d", paramValue);
}
```

### LOG_AND_BAIL_ON_NTSTATUS
Checks an NT status and logs error if it indicates failure, then bails.

**Usage:**
```c
LOG_AND_BAIL_ON_NTSTATUS(Status, "Operation description", ...);
```

**Example:**
```c
Status = SomeFunction();
LOG_AND_BAIL_ON_NTSTATUS(Status, "Failed to perform operation with parameter %d", param);
```

### LOG_AND_BAIL_ON_NULL
Checks if a pointer is NULL, logs error and bails if true.

**Usage:**
```c
LOG_AND_BAIL_ON_NULL(ptr, StatusVar, ErrorCode, "Allocation description", ...);
```

**Example:**
```c
NewBuffer = ExAllocatePoolZero(NonPagedPoolNx, size, POOLTAG);
LOG_AND_BAIL_ON_NULL(NewBuffer, Status, STATUS_NO_MEMORY, "Failed to allocate buffer of size %u", size);
```

### LOG_AND_BAIL_ON_CONDITION
Generic conditional error logging and bail.

**Usage:**
```c
LOG_AND_BAIL_ON_CONDITION(condition, StatusVar, ErrorCode, "Error description", ...);
```

**Example:**
```c
LOG_AND_BAIL_ON_CONDITION(BufferSize < RequiredSize, Status, STATUS_BUFFER_TOO_SMALL, 
                          "Buffer too small: got %u, need %u", BufferSize, RequiredSize);
```

## Benefits

1. **Unique Attribution**: Each failure path has a unique log message with function:line attribution
2. **Consistent Logging**: All failures are logged consistently using the same pattern
3. **Reduced Boilerplate**: Eliminates repetitive error handling code
4. **Better Debugging**: Makes it easier to identify the exact failure point during debugging
5. **Code Clarity**: Separates the error condition from the logging and handling logic

## Before and After

### Before:
```c
NewProgram = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM);
if (NewProgram == NULL) {
    Status = STATUS_NO_MEMORY;
    goto Exit;
}

Status = RtlSizeTMult(sizeof(XDP_RULE), RuleCount, &AllocationSize);
if (!NT_SUCCESS(Status)) {
    goto Exit;
}
```

### After:
```c
NewProgram = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM);
LOG_AND_BAIL_ON_NULL(NewProgram, Status, STATUS_NO_MEMORY, "Failed to allocate memory for new program");

Status = RtlSizeTMult(sizeof(XDP_RULE), RuleCount, &AllocationSize);
LOG_AND_BAIL_ON_NTSTATUS(Status, "Failed to calculate rule allocation size multiplication");
```

## Implementation Details

- All macros use WPP tracing (TraceError) for logging
- Function name and line number are automatically included for unique attribution
- Macros use do-while(0) pattern to ensure safe usage in all contexts
- All macros expect an "Exit" label to exist in the function
- Format strings support WPP format specifiers like %!STATUS!

## Files Modified

The macros are defined in:
- `src/xdp/inc/xdperror.h` - For XDP driver
- `src/xdplwf/inc/xdperror.h` - For XDP LWF

And included in the respective precomp.h files for project-wide availability.