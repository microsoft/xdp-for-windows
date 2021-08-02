//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

VOID
MpCleanupRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );

NDIS_STATUS
MpCreateRssQueues(
    _Inout_ ADAPTER_CONTEXT *Adapter
    );

VOID
MpSetRss(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ NDIS_RECEIVE_SCALE_PARAMETERS *RssParams,
    _In_ SIZE_T RssParamsLength
    );
