//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ntddk.h>
#include <ndis.h>
#include <ntintsafe.h>
#include <ndis/ndl/nblqueue.h>
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
