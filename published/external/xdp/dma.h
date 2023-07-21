//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

typedef struct _XDP_DMA_CAPABILITIES {
    ULONG Size;
    DEVICE_OBJECT *PhysicalDeviceObject;
} XDP_DMA_CAPABILITIES;

inline
VOID
XdpInitializeDmaCapabilitiesPdo(
    _Out_ XDP_DMA_CAPABILITIES *Capabilities,
    _In_ DEVICE_OBJECT *PhysicalDeviceObject
    )
{
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Size = sizeof(*Capabilities);
    Capabilities->PhysicalDeviceObject = PhysicalDeviceObject;
}

EXTERN_C_END
