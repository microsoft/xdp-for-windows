//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"

//
// The miniport pacing module attempts to simulate varying load levels and
// completion latency using timers instead of hardware queues.
//

VOID
MpPaceInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    if (InterlockedExchange8((CHAR *)&RssQueue->Pacing.HwArmed, FALSE)) {
        NdisRequestPoll(RssQueue->NdisPollHandle, 0);
    }
}

static
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(NDIS_TIMER_FUNCTION)
VOID
MpPaceTimeout(
    VOID *SystemSpecific1,
    VOID *FunctionContext,
    VOID *SystemSpecific2,
    VOID *SystemSpecific3
    )
{
    ADAPTER_QUEUE *RssQueue = (ADAPTER_QUEUE *)FunctionContext;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    MpPaceInterrupt(RssQueue);
}

VOID
MpPace(
    _Inout_ ADAPTER_QUEUE *RssQueue
    )
{
    UINT32 RxFrameRate = ReadUInt32Acquire(&RssQueue->Pacing.RxFrameRate);
    UINT32 TxFrameRate = ReadUInt32Acquire(&RssQueue->Pacing.TxFrameRate);

    //
    // Produce continuous batches of frames while the hardware queue is
    // active.
    //
    if (RxFrameRate == MAXUINT32) {
        RssQueue->Rq.PacingFramesAvailable = RxFrameRate;
    }
    if (TxFrameRate == MAXUINT32) {
        RssQueue->Tq.PacingFramesAvailable = TxFrameRate;
    }

    //
    // If RX or TX are rate-limited, check the current time and produce the next
    // batch of frames if the rate interval expired.
    //
    if (RxFrameRate < MAXUINT32 || TxFrameRate < MAXUINT32) {
        LARGE_INTEGER CurrentQpc = KeQueryPerformanceCounter(NULL);

        if (CurrentQpc.QuadPart >= RssQueue->Pacing.ExpirationQpc) {
            //
            // Timer expired. Produce a new batch of frames.
            //
            RssQueue->Rq.PacingFramesAvailable = RxFrameRate;
            RssQueue->Tq.PacingFramesAvailable = TxFrameRate;

            //
            // Set next timeout. If we missed an entire interval, restart from
            // scratch.
            //
            RssQueue->Pacing.ExpirationQpc += RssQueue->Pacing.IntervalQpc;
            if (RssQueue->Pacing.ExpirationQpc <= CurrentQpc.QuadPart) {
                RssQueue->Pacing.ExpirationQpc = CurrentQpc.QuadPart + RssQueue->Pacing.IntervalQpc;
            }
        }

        ASSERT(CurrentQpc.QuadPart < RssQueue->Pacing.ExpirationQpc);
    }

    if (!RssQueue->HwActiveRx) {
        RssQueue->Rq.PacingFramesAvailable = 0;
    }

    RssQueue->Rq.PacingFramesAvailable -=
        HwRingHwComplete(RssQueue->Rq.HwRing, RssQueue->Rq.PacingFramesAvailable);
    RssQueue->Tq.PacingFramesAvailable -=
        HwRingHwComplete(RssQueue->Tq.HwRing, RssQueue->Tq.PacingFramesAvailable);
}

VOID
MpPaceEnableInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    LARGE_INTEGER CurrentQpc;
    LARGE_INTEGER DueTime;
    PKEVENT CleanupEvent;

    //
    // If the queue is being torn down, do not set a new timer.
    //
    CleanupEvent = ReadPointerNoFence(&RssQueue->Pacing.CleanupEvent);
    if (CleanupEvent != NULL) {
        RssQueue->Pacing.CleanupEvent = NULL;
        RssQueue->Pacing.TimerHandle = NULL;
        KeSetEvent(CleanupEvent, 0, FALSE);
    }
    if (RssQueue->Pacing.TimerHandle == NULL) {
        return;
    }

    //
    // If the timer is already armed, do nothing.
    //
    if (InterlockedExchange8((CHAR *)&RssQueue->Pacing.HwArmed, TRUE)) {
        return;
    }

    CurrentQpc = KeQueryPerformanceCounter(NULL);

    //
    // Set next timeout. If we missed an entire interval, restart from
    // scratch.
    //
    if (CurrentQpc.QuadPart >= RssQueue->Pacing.ExpirationQpc) {
        RssQueue->Pacing.ExpirationQpc += RssQueue->Pacing.IntervalQpc;
        if (RssQueue->Pacing.ExpirationQpc <= CurrentQpc.QuadPart) {
            RssQueue->Pacing.ExpirationQpc = CurrentQpc.QuadPart + RssQueue->Pacing.IntervalQpc;
        }
    }

    ASSERT(CurrentQpc.QuadPart < RssQueue->Pacing.ExpirationQpc);
    DueTime.QuadPart = RssQueue->Pacing.ExpirationQpc - CurrentQpc.QuadPart;
    DueTime.QuadPart *= -10i64 * 1000 * 1000;
    DueTime.QuadPart /= RssQueue->Pacing.FrequencyQpc;

    NdisSetTimerObject(RssQueue->Pacing.TimerHandle, DueTime, 0, NULL);
}

NDIS_STATUS
MpUpdatePace(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ CONST XDPMP_PACING_WMI *PacingWmi
    )
{

    if (PacingWmi->RxFramesPerInterval == 0 || PacingWmi->TxFramesPerInterval == 0) {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Adapter->Pacing = *PacingWmi;

    for (UINT32 Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        //
        // For simplicity, only frame rate is currently implemented.
        //
        RssQueue->Pacing.RxFrameRate = PacingWmi->RxFramesPerInterval;
        RssQueue->Pacing.TxFrameRate = PacingWmi->TxFramesPerInterval;
    }

    return NDIS_STATUS_SUCCESS;
}

VOID
MpStartPace(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    LARGE_INTEGER DueTime;
    DueTime.QuadPart = -1;   // Immediately.
    NdisSetTimerObject(RssQueue->Pacing.TimerHandle, DueTime, 0, NULL);
}

NDIS_STATUS
MpInitializePace(
    _Inout_ ADAPTER_QUEUE *RssQueue,
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_TIMER_CHARACTERISTICS TimerParams = {0};
    LARGE_INTEGER FrequencyQpc;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    KeQueryPerformanceCounter(&FrequencyQpc);

    TimerParams.Header.Type = NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS;
    TimerParams.Header.Revision = NDIS_TIMER_CHARACTERISTICS_REVISION_1;
    TimerParams.Header.Size = sizeof(TimerParams);
    TimerParams.AllocationTag = POOLTAG_QUEUE;
    TimerParams.TimerFunction = MpPaceTimeout;
    TimerParams.FunctionContext = RssQueue;
    Status =
        NdisAllocateTimerObject(
            Adapter->MiniportHandle, &TimerParams, &RssQueue->Pacing.TimerHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    RssQueue->Pacing.HwArmed = TRUE;
    RssQueue->Pacing.FrequencyQpc = FrequencyQpc.QuadPart;
    RssQueue->Pacing.ExpirationQpc = 0;
    RssQueue->Pacing.RxFrameRate = Adapter->Pacing.RxFramesPerInterval;
    RssQueue->Pacing.TxFrameRate = Adapter->Pacing.TxFramesPerInterval;
    RssQueue->Pacing.IntervalQpc =
        (Adapter->Pacing.IntervalUs * RssQueue->Pacing.FrequencyQpc) / 1000 / 1000;

Exit:

    return Status;
}

VOID
MpCleanupPace(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    NDIS_HANDLE TimerHandle = RssQueue->Pacing.TimerHandle;
    KEVENT CleanupEvent;

    //
    // Prevent new timers being armed.
    //
    KeInitializeEvent(&CleanupEvent, NotificationEvent, FALSE);
    RssQueue->Pacing.CleanupEvent = &CleanupEvent;
    NdisRequestPoll(RssQueue->NdisPollHandle, 0);
    KeWaitForSingleObject(&CleanupEvent, Executive, KernelMode, FALSE, NULL);
    ASSERT(RssQueue->Pacing.CleanupEvent == NULL);
    ASSERT(RssQueue->Pacing.TimerHandle == NULL);

    //
    // Cancel and wait for any outstanding timer to execute.
    //
    NdisCancelTimerObject(TimerHandle);
    KeFlushQueuedDpcs();

    NdisFreeTimerObject(TimerHandle);
}
