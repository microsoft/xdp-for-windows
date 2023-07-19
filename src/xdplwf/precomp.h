//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ntdef.h>
#include <ntstatus.h>
#include <ntifs.h>
#include <ntintsafe.h>
#include <ntstrsafe.h>
#include <ndis.h>
#include <ndis/ndl/nblqueue.h>
#include <ndis/ndl/nblclassify.h>
#include <netiodef.h>
#include <qeo_ndis.h>
#include <winerror.h>

#define XDPEXPORT(RoutineName) RoutineName##Thunk

#include <xdpapi.h>
#include <xdpapi_experimental.h>

#include <xdp/buffermdl.h>
#include <xdp/buffervirtualaddress.h>
#include <xdp/control.h>
#include <xdp/datapath.h>
#include <xdp/framefragment.h>
#include <xdp/frameinterfacecontext.h>
#include <xdp/framerxaction.h>
#include <xdp/hookid.h>
#include <xdp/ndis6.h>
#include <xdp/txframecompletioncontext.h>

#include <xdpassert.h>
#include <xdpetw.h>
#include <xdpif.h>
#include <xdplifetime.h>
#include <xdplwf.h>
#include <xdppcw.h>
#include <xdpregistry.h>
#include <xdprtl.h>
#include <xdprxqueue_internal.h>
#include <xdpstatusconvert.h>
#include <xdptimer.h>
#include <xdptxqueue_internal.h>
#include <xdptrace.h>
#include <xdpworkqueue.h>

#ifndef NDIS_RUNTIME_VERSION_688
#define NDIS_RUNTIME_VERSION_688 ((6 << 16) | 88)
#else
C_ASSERT(!"NDIS_RUNTIME_VERSION_688 has been publicly defined. Remove redundant definition.");
#endif

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#include "filter.h"
#include "bind.h"
#include "dispatch.h"
#include "ec.h"
#include "generic.h"
#include "native.h"
#include "offload.h"
#include "offloadqeo.h"
#include "offloadrss.h"
#include "oid.h"
#include "recv.h"
#include "send.h"
