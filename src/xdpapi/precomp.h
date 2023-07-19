//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define XDPAPI __declspec(dllexport)

#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <crtdbg.h>
#include <ifdef.h>
#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include <xdpassert.h>
#include <xdpioctl.h>
#include <xdprtl.h>

#include "ioctl.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
