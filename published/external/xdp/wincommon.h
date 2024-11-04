//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_WINCOMMON_H
#define XDP_WINCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

//
// This file includes all Windows headers necessary for XDP APIs.
//
// Since it is notoriously difficult to include Windows headers without
// introducing compiler errors, this file can be disabled by defining
// NOXDPWINCOMMON and including all Windows dependencies before including XDP
// headers.
//

#ifndef NOXDPWINCOMMON

#ifdef _KERNEL_MODE

//
// TODO
//

#else

//
// Include only the "minimum" Windows headers by default.
//
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN_XDP /* Remember this header set WIN32_LEAN_AND_MEAN. */
#endif

//
// The windows.h header defines some NTSTATUS values, but not all, and conflicts
// with ntstatus.h.
//
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include <ntstatus.h>
#include <winioctl.h>
#include <winternl.h>

#ifdef WIN32_LEAN_AND_MEAN_XDP
#undef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN_XDP
#endif

#endif

#endif // NOXDPWINCOMMON

#ifdef __cplusplus
} // extern "C"
#endif

#endif
