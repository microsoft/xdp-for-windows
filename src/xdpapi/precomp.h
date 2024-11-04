//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define XDPAPI __declspec(dllexport)

#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include <xdpassert.h>
#include <xdprtl.h>

#include <xdp/details/ioctldef.h>

//
// Set the implicit API version to the original API version (v1).
//
#define XDP_API_VERSION XDP_API_VERSION_1
#include <xdp/details/ioctlfn.h>

#include <winternl.h>
#include <crtdbg.h>
#include <ifdef.h>
