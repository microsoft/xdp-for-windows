//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

VOID
MpPace(
    _Inout_ ADAPTER_QUEUE *RssQueue
    );

VOID
MpPaceInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    );

VOID
MpPaceEnableInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    );

NDIS_STATUS
MpUpdatePace(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ CONST XDPMP_PACING_WMI *PacingWmi
    );

VOID
MpStartPace(
    _In_ ADAPTER_QUEUE *RssQueue
    );

NDIS_STATUS
MpInitializePace(
    _Inout_ ADAPTER_QUEUE *RssQueue,
    _In_ ADAPTER_CONTEXT *Adapter
    );

VOID
MpCleanupPace(
    _In_ ADAPTER_QUEUE *RssQueue
    );
