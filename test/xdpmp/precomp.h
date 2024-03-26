//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ntintsafe.h>
#include <netiodef.h>
#include <pkthlp.h>
#include <xdpddi.h>
#include <xdpassert.h>
#include <xdprtl.h>
#include <fndispoll.h>
#include <fndisnpi.h>

#include "xdpmpwmi.h"
#include "hwring.h"
#include "miniport.h"
#include "poll.h"
#include "ratesim.h"
#include "rss.h"
#include "rx.h"
#include "trace.h"
#include "tx.h"

#define POOLTAG_ADAPTER  'ApmX' // XmpA
#define POOLTAG_RXBUFFER 'BpmX' // XmpB
#define POOLTAG_HWRING   'HpmX' // XmpH
#define POOLTAG_NBL      'NpmX' // XmpN
#define POOLTAG_QUEUE    'QpmX' // XmpQ
#define POOLTAG_RSS      'RpmX' // XmpR
#define POOLTAG_TX       'TpmX' // XmpT
