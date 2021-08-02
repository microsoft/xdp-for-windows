//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <wdm.h>
#include <ndis.h>
#include <ntintsafe.h>
#include <pkthlp.h>
#include <xdpddi.h>
#include <xdpassert.h>

#include "xdpmpwmi.h"
#include "hwring.h"
#include "miniport.h"
#include "pace.h"
#include "rss.h"
#include "rx.h"
#include "tx.h"

#define POOLTAG_ADAPTER  'ApmX' // XmpA
#define POOLTAG_RXBUFFER 'BpmX' // XmpB
#define POOLTAG_HWRING   'HpmX' // XmpH
#define POOLTAG_NBL      'NpmX' // XmpN
#define POOLTAG_QUEUE    'QpmX' // XmpQ
#define POOLTAG_RSS      'RpmX' // XmpR
#define POOLTAG_TX       'TpmX' // XmpT

#define RTL_IS_POWER_OF_TWO(Value) \
    ((Value != 0) && !((Value) & ((Value) - 1)))

#ifdef FNDIS
#define NdisRegisterPoll        FNdisRegisterPoll
#define NdisDeregisterPoll      FNdisDeregisterPoll
#define NdisSetPollAffinity     FNdisSetPollAffinity
#define NdisRequestPoll         FNdisRequestPoll
#define XdpCompleteNdisPoll     FNdisCompletePoll
#include <fndispoll.h>
#endif
