//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#define XDPAPI __declspec(dllexport)

#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <ifdef.h>
#include <msxdp.h>
#include <xdpioctl.h>
#include <xdprtl.h>

#include "ioctl.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
