//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This top-level header includes all XDP headers for driver developers.
//

//
// Include all necessary Windows headers first.
//
#include <xdp/wincommon.h>

#include <xdp/apiversion.h>
#include <xdp/bufferinterfacecontext.h>
#include <xdp/bufferlogicaladdress.h>
#include <xdp/buffermdl.h>
#include <xdp/buffervirtualaddress.h>
#include <xdp/control.h>
#include <xdp/datapath.h>
#include <xdp/dma.h>
#include <xdp/driverapi.h>
#include <xdp/extension.h>
#include <xdp/extensioninfo.h>
#include <xdp/framechecksum.h>
#include <xdp/framechecksumextension.h>
#include <xdp/framefragment.h>
#include <xdp/frameinterfacecontext.h>
#include <xdp/framelayout.h>
#include <xdp/framelayoutextension.h>
#include <xdp/framerxaction.h>
#include <xdp/guid.h>
#include <xdp/interfaceconfig.h>
#include <xdp/ndis6.h>
#include <xdp/ndis6poll.h>
#include <xdp/objectheader.h>
#include <xdp/pollinfo.h>
#include <xdp/queueinfo.h>
#include <xdp/rtl.h>
#include <xdp/rxqueueconfig.h>
#include <xdp/txframecompletioncontext.h>
#include <xdp/txqueueconfig.h>
