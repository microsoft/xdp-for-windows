//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <ntddk.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <mstcpip.h>
#include <pkthlp.h>

#define htons RtlUshortByteSwap
