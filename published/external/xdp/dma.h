//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

//
// Structure defining the DMA capabilities of an XDP queue.
//
typedef struct _XDP_DMA_CAPABILITIES {
    ULONG Size;

    //
    // Specifies the physical device object to map to.
    //
    DEVICE_OBJECT *PhysicalDeviceObject;
} XDP_DMA_CAPABILITIES;

//
// Initializes DMA capabilities for system-mapped or system-allocated buffers
// and device logical addresses.
//
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
