//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ntintsafe.h>
#include <ndis/nblqueue.h>
#include <xdpassert.h>
#include <xdprtl.h>

#include <generic_x.h>
#include <ioctlbounce.h>
#include <miniport.h>
#include <pooltag.h>
#include <xdpfnmpioctl.h>

#include "generic.h"
#include "rx.h"
#include "tx.h"
