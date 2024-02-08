//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDPAPI_STATUS_H
#define XDPAPI_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_KERNEL_MODE)

typedef NTSTATUS XDP_STATUS;

#define XDP_FAILED(X)                      (!NT_SUCCESS(X))
#define XDP_SUCCEEDED(X)                   NT_SUCCESS(X)

#define XDP_STATUS_SUCCESS                 STATUS_SUCCESS
#define XDP_STATUS_PENDING                 STATUS_PENDING
#define XDP_STATUS_TIMEOUT                 STATUS_TIMEOUT

#else

typedef HRESULT XDP_STATUS;

#define XDP_FAILED(X)                      FAILED(X)
#define XDP_SUCCEEDED(X)                   SUCCEEDED(X)

#define XDP_STATUS_SUCCESS                 S_OK
#define XDP_STATUS_PENDING                 HRESULT_FROM_WIN32(ERROR_IO_PENDING)
#define XDP_STATUS_TIMEOUT                 HRESULT_FROM_WIN32(ERROR_TIMEOUT)

#endif // defined(_KERNEL_MODE)

#ifdef __cplusplus
} // extern "C"
#endif

#endif
