//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <ntddk.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <mstcpip.h>
#include <pkthlp.h>

#define htons RtlUshortByteSwap
#define ntohs RtlUshortByteSwap
#define htonl RtlUlongByteSwap
#define ntohl RtlUlongByteSwap
