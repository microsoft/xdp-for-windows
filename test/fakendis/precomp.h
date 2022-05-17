//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <ntdef.h>
#include <ntstatus.h>
#include <ntifs.h>
#include <ntintsafe.h>
#include <ndis.h>
#include <fndisioctl.h>
#include <fndisnpi.h>
#include <fndispoll_p.h>
#include <xdpassert.h>
#include <xdppollbackchannel.h>
#include <xdprtl.h>

#include "driver.h"
#include "poll.h"
#include "pollbackchannel.h"
#include "polldpc.h"
#include "trace.h"

#define POOLTAG_BACKCHANNEL 'BinF'  // FniB
#define POOLTAG_POLL        'PinF'  // FniP
#define POOLTAG_PERCPU      'CinF'  // FniC
#define POOLTAG_FILE        'FinF'  // FniF
