//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <ntosp.h>
#include <zwapi.h>
#include <ntintsafe.h>
#include <ntrtl.h>
#include <ndis.h>
#include <ndis/nblqueue.h>
#include <ndis/nblclassify.h>
#include <netiodef.h>

#define XDPEXPORT(RoutineName) RoutineName##Thunk

#include <xdp/buffermdl.h>
#include <xdp/buffervirtualaddress.h>
#include <xdp/control.h>
#include <xdp/datapath.h>
#include <xdp/framefragment.h>
#include <xdp/hookid.h>
#include <xdp/ndis6.h>

#include <xdpassert.h>
#include <xdplifetime.h>
#include <xdpif.h>
#include <xdplwf.h>
#include <xdpregistry.h>
#include <xdprtl.h>
#include <xdprxqueue_internal.h>
#include <xdpstatusconvert.h>
#include <xdptxqueue_internal.h>
#include <xdptrace.h>

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#include "filter.h"
#include "bind.h"
#include "dispatch.h"
#include "ec.h"
#include "generic.h"
#include "native.h"
#include "oid.h"
#include "recv.h"
#include "send.h"
