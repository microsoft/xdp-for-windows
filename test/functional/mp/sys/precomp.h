//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ntintsafe.h>
#include <xdpddi.h>
#include <xdpassert.h>

#include <generic_x.h>
#include <native_x.h>

#include "bounce.h"
#include "dispatch.h"
#include "ioctlbounce.h"
#include "miniport.h"
#include "pooltag.h"
#include "oid.h"
#include "rss.h"
#include "xdpfnmpioctl.h"

#define RTL_IS_POWER_OF_TWO(Value) \
    ((Value != 0) && !((Value) & ((Value) - 1)))
