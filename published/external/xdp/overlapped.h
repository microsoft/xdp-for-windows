//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_OVERLAPPED_H
#define XDP_OVERLAPPED_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL_MODE

typedef struct _OVERLAPPED OVERLAPPED;

typedef struct _XDP_OVERLAPPED {
    IO_STATUS_BLOCK Internal;
    HANDLE hEvent;
} XDP_OVERLAPPED;

#else

typedef OVERLAPPED XDP_OVERLAPPED;

#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
