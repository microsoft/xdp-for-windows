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

#define RTL_IS_POWER_OF_TWO(Value) \
    ((Value != 0) && !((Value) & ((Value) - 1)))

#ifndef NTDDI_WIN10_CO
FORCEINLINE
UINT32
ReadUInt32Acquire (
    _In_ _Interlocked_operand_ UINT32 const volatile *Source
    )
{
    return (UINT32)ReadAcquire((PLONG)Source);
}
CFORCEINLINE
VOID
WriteUInt32Release (
    _Out_ _Interlocked_operand_ UINT32 volatile *Destination,
    _In_ UINT32 Value
    )
{
    WriteRelease((PLONG)Destination, (LONG)Value);
    return;
}
#endif // NTDDI_WIN10_CO

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

#ifdef FNDIS
#define NdisRegisterPoll        FNdisRegisterPoll
#define NdisDeregisterPoll      FNdisDeregisterPoll
#define NdisSetPollAffinity     FNdisSetPollAffinity
#define NdisRequestPoll         FNdisRequestPoll
#define XdpCompleteNdisPoll     FNdisCompletePoll
#include <fndispoll.h>
#endif
