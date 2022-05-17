//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

VOID
MpRateSim(
    _Inout_ ADAPTER_QUEUE *RssQueue
    );

VOID
MpRateSimInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    );

VOID
MpRateSimEnableInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    );

NDIS_STATUS
MpUpdateRateSim(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ CONST XDPMP_RATE_SIM_WMI *RateSimWmi
    );

VOID
MpStartRateSim(
    _In_ ADAPTER_QUEUE *RssQueue
    );

NDIS_STATUS
MpInitializeRateSim(
    _Inout_ ADAPTER_QUEUE *RssQueue,
    _In_ ADAPTER_CONTEXT *Adapter
    );

VOID
MpCleanupRateSim(
    _In_ ADAPTER_QUEUE *RssQueue
    );
