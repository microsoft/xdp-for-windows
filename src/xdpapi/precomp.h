//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define XDPAPI __declspec(dllexport)

//
// Set the implicit API version to the original API version (v1).
//
#ifdef XDP_API_VERSION
#undef XDP_API_VERSION
#endif
#define XDP_API_VERSION XDP_API_VERSION_1

#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include <xdpassert.h>
#include <xdprtl.h>

#include <xdp/details/afxdp.h>
#include <xdp/details/ioctldef.h>
#include <xdp/details/ioctlfn.h>
#include <xdp/details/xdpapi.h>
#include <xdp/details/xdpapi_experimental.h>

#include <winternl.h>
#include <crtdbg.h>
#include <ifdef.h>
