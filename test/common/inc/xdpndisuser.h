//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define NDIS_STATUS_SUCCESS                     ((NDIS_STATUS)STATUS_SUCCESS)
#define NDIS_STATUS_PENDING                     ((NDIS_STATUS)STATUS_PENDING)
#define NDIS_STATUS_FAILURE                     ((NDIS_STATUS)STATUS_UNSUCCESSFUL)

//
// offload specific status indication codes
//
#define NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG         ((NDIS_STATUS)0x40020006L)
#define NDIS_STATUS_TASK_OFFLOAD_HARDWARE_CAPABILITIES  ((NDIS_STATUS)0x40020007L)

#ifdef __cplusplus
} // extern "C"
#endif
