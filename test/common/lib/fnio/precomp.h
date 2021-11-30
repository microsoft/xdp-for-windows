//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ntddk.h>
#include <ntintsafe.h>
#include <ndis.h>
#include <ndis/ndl/nblqueue.h>
#include <ntintsafe.h>

#include <xdpassert.h>
#include <xdprtl.h>

#include <bounce.h>
#include <fnio.h>

#include "ioctlbounce.h"
#include "pooltag.h"
