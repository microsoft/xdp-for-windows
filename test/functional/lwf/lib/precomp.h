//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <ifdef.h>
#include <ntddndis.h>
#include <xdpfnlwfapi.h>
#include <xdpfnlwfioctl.h>
#include <xdprtl.h>

#include "ioctl.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif
