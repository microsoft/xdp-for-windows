//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#if defined(_KERNEL_MODE)

#define XDP_STATUS NTSTATUS

#define XDP_FAILED(X)                      (!NT_SUCCESS(X))
#define XDP_SUCCEEDED(X)                   NT_SUCCESS(X)

#define XDP_STATUS_SUCCESS                 STATUS_SUCCESS
#define XDP_STATUS_PENDING                 STATUS_PENDING
#define XDP_STATUS_TIMEOUT                 STATUS_TIMEOUT
#define XDP_STATUS_OUT_OF_MEMORY           STATUS_NO_MEMORY
#define XDP_STATUS_INTERNAL_ERROR          STATUS_INTERNAL_ERROR
#define XDP_STATUS_NOT_SUPPORTED           STATUS_NOT_SUPPORTED

#else

#define XDP_STATUS HRESULT

#define XDP_FAILED(X)                      FAILED(X)
#define XDP_SUCCEEDED(X)                   SUCCEEDED(X)

#define XDP_STATUS_SUCCESS                 S_OK
#define XDP_STATUS_PENDING                 HRESULT_FROM_WIN32(ERROR_IO_PENDING)
#define XDP_STATUS_TIMEOUT                 HRESULT_FROM_WIN32(ERROR_TIMEOUT)
#define XDP_STATUS_OUT_OF_MEMORY           E_OUTOFMEMORY
#define XDP_STATUS_INTERNAL_ERROR          HRESULT_FROM_WIN32(ERROR_INTERNAL_ERROR)
#define XDP_STATUS_NOT_SUPPORTED           HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)

#endif // defined(_KERNEL_MODE)

EXTERN_C_END
