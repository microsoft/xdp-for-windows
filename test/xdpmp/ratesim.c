//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

//
// The miniport rate simulator module attempts to emulate varying load levels
// and completion latency using timers instead of hardware queues. This module
// is for testing only and is not necessary for NICs connected to a medium.
//

VOID
MpRateSimInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    if (InterlockedExchange8((CHAR *)&RssQueue->RateSim.HwArmed, FALSE)) {
        RssQueue->Adapter->PollDispatch.RequestPoll(RssQueue->NdisPollHandle, 0);
    }
}

static
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
_Function_class_(NDIS_TIMER_FUNCTION)
VOID
MpRateSimTimeout(
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

    MpRateSimInterrupt(RssQueue);
}

VOID
MpRateSim(
    _Inout_ ADAPTER_QUEUE *RssQueue
    )
{
    UINT32 RxFrameRate = ReadUInt32Acquire(&RssQueue->RateSim.RxFrameRate);
    UINT32 TxFrameRate = ReadUInt32Acquire(&RssQueue->RateSim.TxFrameRate);

    //
    // Produce continuous batches of frames while the hardware queue is
    // active.
    //
    if (RxFrameRate == MAXUINT32) {
        RssQueue->Rq.RateSimFramesAvailable = RxFrameRate;
    }
    if (TxFrameRate == MAXUINT32) {
        RssQueue->Tq.RateSimFramesAvailable = TxFrameRate;
    }

    //
    // If RX or TX are rate-limited, check the current time and produce the next
    // batch of frames if the rate interval expired.
    //
    if (RxFrameRate < MAXUINT32 || TxFrameRate < MAXUINT32) {
        LARGE_INTEGER CurrentQpc = KeQueryPerformanceCounter(NULL);

        if (CurrentQpc.QuadPart >= RssQueue->RateSim.ExpirationQpc) {
            //
            // Timer expired. Produce a new batch of frames.
            //
            RssQueue->Rq.RateSimFramesAvailable = RxFrameRate;
            RssQueue->Tq.RateSimFramesAvailable = TxFrameRate;

            //
            // Set next timeout. If we missed an entire interval, restart from
            // scratch.
            //
            RssQueue->RateSim.ExpirationQpc += RssQueue->RateSim.IntervalQpc;
            if (RssQueue->RateSim.ExpirationQpc <= CurrentQpc.QuadPart) {
                RssQueue->RateSim.ExpirationQpc = CurrentQpc.QuadPart + RssQueue->RateSim.IntervalQpc;
            }
        }

        ASSERT(CurrentQpc.QuadPart < RssQueue->RateSim.ExpirationQpc);
    }

    if (!RssQueue->HwActiveRx) {
        RssQueue->Rq.RateSimFramesAvailable = 0;
    }

    RssQueue->Rq.RateSimFramesAvailable -=
        HwRingHwComplete(RssQueue->Rq.HwRing, RssQueue->Rq.RateSimFramesAvailable);
    RssQueue->Tq.RateSimFramesAvailable -=
        HwRingHwComplete(RssQueue->Tq.HwRing, RssQueue->Tq.RateSimFramesAvailable);
}

VOID
MpRateSimEnableInterrupt(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    LARGE_INTEGER CurrentQpc;
    LARGE_INTEGER DueTime;
    PKEVENT CleanupEvent;

    //
    // If the queue is being torn down, do not set a new timer.
    //
    CleanupEvent = ReadPointerNoFence(&RssQueue->RateSim.CleanupEvent);
    if (CleanupEvent != NULL) {
        RssQueue->RateSim.CleanupEvent = NULL;
        RssQueue->RateSim.TimerHandle = NULL;
        KeSetEvent(CleanupEvent, 0, FALSE);
    }
    if (RssQueue->RateSim.TimerHandle == NULL) {
        return;
    }

    //
    // If the timer is already armed, do nothing.
    //
    if (InterlockedExchange8((CHAR *)&RssQueue->RateSim.HwArmed, TRUE)) {
        return;
    }

    CurrentQpc = KeQueryPerformanceCounter(NULL);

    //
    // Set next timeout. If we missed an entire interval, restart from
    // scratch.
    //
    if (CurrentQpc.QuadPart >= RssQueue->RateSim.ExpirationQpc) {
        RssQueue->RateSim.ExpirationQpc += RssQueue->RateSim.IntervalQpc;
        if (RssQueue->RateSim.ExpirationQpc <= CurrentQpc.QuadPart) {
            RssQueue->RateSim.ExpirationQpc = CurrentQpc.QuadPart + RssQueue->RateSim.IntervalQpc;
        }
    }

    ASSERT(CurrentQpc.QuadPart < RssQueue->RateSim.ExpirationQpc);
    DueTime.QuadPart = RssQueue->RateSim.ExpirationQpc - CurrentQpc.QuadPart;
    DueTime.QuadPart *= -10i64 * 1000 * 1000;
    DueTime.QuadPart /= RssQueue->RateSim.FrequencyQpc;

    NdisSetTimerObject(RssQueue->RateSim.TimerHandle, DueTime, 0, NULL);
}

NDIS_STATUS
MpUpdateRateSim(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ CONST XDPMP_RATE_SIM_WMI *RateSimWmi
    )
{

    if (RateSimWmi->RxFramesPerInterval == 0 || RateSimWmi->TxFramesPerInterval == 0) {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Adapter->RateSim = *RateSimWmi;

    for (UINT32 Index = 0; Index < Adapter->NumRssQueues; Index++) {
        ADAPTER_QUEUE *RssQueue = &Adapter->RssQueues[Index];

        //
        // For simplicity, only frame rate is currently implemented.
        //
        RssQueue->RateSim.RxFrameRate = RateSimWmi->RxFramesPerInterval;
        RssQueue->RateSim.TxFrameRate = RateSimWmi->TxFramesPerInterval;
    }

    return NDIS_STATUS_SUCCESS;
}

VOID
MpStartRateSim(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    LARGE_INTEGER DueTime;
    DueTime.QuadPart = -1;   // Immediately.
    NdisSetTimerObject(RssQueue->RateSim.TimerHandle, DueTime, 0, NULL);
}

NDIS_STATUS
MpInitializeRateSim(
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
    TimerParams.TimerFunction = MpRateSimTimeout;
    TimerParams.FunctionContext = RssQueue;
    Status =
        NdisAllocateTimerObject(
            Adapter->MiniportHandle, &TimerParams, &RssQueue->RateSim.TimerHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    RssQueue->RateSim.HwArmed = TRUE;
    RssQueue->RateSim.FrequencyQpc = FrequencyQpc.QuadPart;
    RssQueue->RateSim.ExpirationQpc = 0;
    RssQueue->RateSim.RxFrameRate = Adapter->RateSim.RxFramesPerInterval;
    RssQueue->RateSim.TxFrameRate = Adapter->RateSim.TxFramesPerInterval;
    RssQueue->RateSim.IntervalQpc =
        (Adapter->RateSim.IntervalUs * RssQueue->RateSim.FrequencyQpc) / 1000 / 1000;

Exit:

    return Status;
}

VOID
MpCleanupRateSim(
    _In_ ADAPTER_QUEUE *RssQueue
    )
{
    NDIS_HANDLE TimerHandle = RssQueue->RateSim.TimerHandle;
    KEVENT CleanupEvent;

    //
    // Prevent new timers being armed.
    //
    KeInitializeEvent(&CleanupEvent, NotificationEvent, FALSE);
    RssQueue->RateSim.CleanupEvent = &CleanupEvent;
    RssQueue->Adapter->PollDispatch.RequestPoll(RssQueue->NdisPollHandle, 0);
    KeWaitForSingleObject(&CleanupEvent, Executive, KernelMode, FALSE, NULL);
    ASSERT(RssQueue->RateSim.CleanupEvent == NULL);
    ASSERT(RssQueue->RateSim.TimerHandle == NULL);

    //
    // Cancel and wait for any outstanding timer to execute.
    //
    NdisCancelTimerObject(TimerHandle);
    KeFlushQueuedDpcs();

    NdisFreeTimerObject(TimerHandle);
}
