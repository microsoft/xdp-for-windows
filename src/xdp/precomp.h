//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#if USER_MODE
#include <precomp.h>
#else

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ntdef.h>
#include <ntstatus.h>
#include <ntifs.h>
#include <ntintsafe.h>
#include <ndis.h>
#include <wdmsec.h>

//
// The stdint.h header is included by eBPF headers, and stdint.h throws
// warnings. Include it directly and suppress the warnings.
//
#pragma warning(push)
#pragma warning(disable:4083)
#pragma warning(disable:4005)
#include <stdint.h>
#pragma warning(pop)
#include <stdlib.h>
#include <netiodef.h>
#include <netioddk.h>
#include <winerror.h>
#include <ntstrsafe.h>
#include <ebpf_extension.h>
#include <ebpf_extension_uuids.h>
#include <ebpf_nethooks.h>
#include <ebpf_program_attach_type_guids.h>
#include <ebpf_program_types.h>
#include <ebpf_result.h>
#include <ebpf_structs.h>
#include "ebpf_private_extension.h"

#define XDPAPI
#define XDPEXPORT(RoutineName) RoutineName##Thunk

#include <xdp/bufferinterfacecontext.h>
#include <xdp/bufferlogicaladdress.h>
#include <xdp/buffermdl.h>
#include <xdp/buffervirtualaddress.h>
#include <xdp/control.h>
#include <xdp/datapath.h>
#include <xdp/framefragment.h>
#include <xdp/frameinterfacecontext.h>
#include <xdp/framerxaction.h>
#include <xdp/txframecompletioncontext.h>

#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include <xdpassert.h>
#include <xdpetw.h>
#include <xdpif.h>
#include <xdpioctl.h>
#include <xdplwf.h>
#include <xdppcw.h>
#include <xdpnmrprovider.h>
#include <xdppollshim.h>
#include <xdprefcount.h>
#include <xdpregistry.h>
#include <xdprtl.h>
#include <xdprxqueue_internal.h>
#include <xdptrace.h>
#include <xdptransport.h>
#include <xdptxqueue_internal.h>
#include <xdpversion.h>
#include <xdpworkqueue.h>

#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#include "xdpp.h"
#include "bind.h"
#include "dispatch.h"
#include "ebpfextension.h"
#include "extensionset.h"
#include "offload.h"
#include "offloadqeo.h"
#include "program.h"
#include "queue.h"
#include "redirect.h"
#include "ring.h"
#include "rx.h"
#include "tx.h"
#include "xsk.h"

#endif // USER_MODE
