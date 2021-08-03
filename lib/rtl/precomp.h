//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <ntosp.h>
#include <zwapi.h>
#include <ntintsafe.h>
#include <ntrtl.h>

#include <xdpassert.h>
#include <xdplifetime.h>
#include <xdpregistry.h>
#include <xdprtl.h>
#include <xdptimer.h>
#include <xdptrace.h>
#include <xdpworkqueue.h>

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#define XDP_POOLTAG_LIFETIME    'LcdX' // XdcL
#define XDP_POOLTAG_REGISTRY    'RcdX' // XdcR
#define XDP_POOLTAG_TIMER       'TcdX' // XdcT
#define XDP_POOLTAG_WORKQUEUE   'WcdX' // XdcW
