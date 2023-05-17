//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "offloadqeo.tmh"

NTSTATUS
XdpLwfOffloadQeoSet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _In_ const XDP_OFFLOAD_PARAMS_QEO *XdpQeoParams,
    _In_ UINT32 XdpQeoParamsSize,
    _Out_opt_ VOID *OffloadResult,
    _In_ UINT32 OffloadResultSize,
    _Out_opt_ UINT32 *OffloadResultWritten
    )
{
    NTSTATUS Status;
    BOOLEAN RundownAcquired = FALSE;
    const XDP_QUIC_CONNECTION *XdpQeoConnection;
    NDIS_QUIC_CONNECTION *NdisConnections = NULL;
    UINT32 NdisConnectionsSize;
    ULONG NdisBytesReturned;

    TraceEnter(TRACE_LWF, "Filter=%p OffloadContext=%p", Filter, OffloadContext);

    if (!ExAcquireRundownProtection(&Filter->Offload.FilterRundown)) {
        Status = STATUS_DELETE_PENDING;
        goto Exit;
    }

    RundownAcquired = TRUE;

    if (XdpQeoParamsSize != sizeof(*XdpQeoParams) ||
        XdpQeoParams->ConnectionCount == 0 ||
        OffloadResultSize < XdpQeoParams->ConnectionsSize) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (OffloadContext->Edge != XdpOffloadEdgeLower) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    //
    // Clone the input params.
    //

    Status =
        RtlUInt32Mult(
            XdpQeoParams->ConnectionCount, sizeof(*NdisConnections), &NdisConnectionsSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    NdisConnections = ExAllocatePoolZero(NonPagedPoolNx, NdisConnectionsSize, POOLTAG_OFFLOAD);
    if (NdisConnections == NULL) {
        TraceError(
            TRACE_LWF, "OffloadContext=%p Failed to allocate NDIS connections", OffloadContext);
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    //
    // Form and issue the OID.
    //

    XdpQeoConnection = XdpQeoParams->Connections;

    for (UINT32 i = 0; i < XdpQeoParams->ConnectionCount; i++) {
        NDIS_QUIC_CONNECTION *NdisConnection = &NdisConnections[i];

        switch (XdpQeoConnection->Operation) {
        case XDP_QUIC_OPERATION_ADD:
            NdisConnection->Operation = NDIS_QUIC_OPERATION_ADD;
            break;
        case XDP_QUIC_OPERATION_REMOVE:
            NdisConnection->Operation = NDIS_QUIC_OPERATION_REMOVE;
            break;
        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        switch (XdpQeoConnection->Direction) {
        case XDP_QUIC_DIRECTION_TRANSMIT:
            NdisConnection->Direction = NDIS_QUIC_DIRECTION_TRANSMIT;
            break;
        case XDP_QUIC_DIRECTION_RECEIVE:
            NdisConnection->Direction = NDIS_QUIC_DIRECTION_RECEIVE;
            break;
        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        switch (XdpQeoConnection->DecryptFailureAction) {
        case XDP_QUIC_DECRYPT_FAILURE_ACTION_DROP:
            NdisConnection->DecryptFailureAction = NDIS_QUIC_DECRYPT_FAILURE_ACTION_DROP;
            break;
        case XDP_QUIC_DECRYPT_FAILURE_ACTION_CONTINUE:
            NdisConnection->DecryptFailureAction = NDIS_QUIC_DECRYPT_FAILURE_ACTION_CONTINUE;
            break;
        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        NdisConnection->KeyPhase = XdpQeoConnection->KeyPhase;

        switch (XdpQeoConnection->CipherType) {
        case XDP_QUIC_CIPHER_TYPE_AEAD_AES_128_GCM:
            NdisConnection->CipherType = NDIS_QUIC_CIPHER_TYPE_AEAD_AES_128_GCM;
            break;
        case XDP_QUIC_CIPHER_TYPE_AEAD_AES_256_GCM:
            NdisConnection->CipherType = NDIS_QUIC_CIPHER_TYPE_AEAD_AES_256_GCM;
            break;
        case XDP_QUIC_CIPHER_TYPE_AEAD_CHACHA20_POLY1305:
            NdisConnection->CipherType = NDIS_QUIC_CIPHER_TYPE_AEAD_CHACHA20_POLY1305;
            break;
        case XDP_QUIC_CIPHER_TYPE_AEAD_AES_128_CCM:
            NdisConnection->CipherType = NDIS_QUIC_CIPHER_TYPE_AEAD_AES_128_CCM;
            break;
        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        switch (XdpQeoConnection->AddressFamily) {
        case XDP_QUIC_ADDRESS_FAMILY_INET4:
            NdisConnection->AddressFamily = NDIS_QUIC_ADDRESS_FAMILY_INET4;
            break;
        case XDP_QUIC_ADDRESS_FAMILY_INET6:
            NdisConnection->AddressFamily = NDIS_QUIC_ADDRESS_FAMILY_INET6;
            break;
        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        NdisConnection->UdpPort = XdpQeoConnection->UdpPort;
        NdisConnection->NextPacketNumber = XdpQeoConnection->NextPacketNumber;
        NdisConnection->ConnectionIdLength = XdpQeoConnection->ConnectionIdLength;

        C_ASSERT(sizeof(NdisConnection->Address) >= sizeof(XdpQeoConnection->Address));
        RtlCopyMemory(
            NdisConnection->Address, XdpQeoConnection->Address,
            sizeof(XdpQeoConnection->Address));

        C_ASSERT(sizeof(NdisConnection->ConnectionId) >= sizeof(XdpQeoConnection->ConnectionId));
        RtlCopyMemory(
            NdisConnection->ConnectionId, XdpQeoConnection->ConnectionId,
            sizeof(XdpQeoConnection->ConnectionId));

        C_ASSERT(sizeof(NdisConnection->PayloadKey) >= sizeof(XdpQeoConnection->PayloadKey));
        RtlCopyMemory(
            NdisConnection->PayloadKey, XdpQeoConnection->PayloadKey,
            sizeof(XdpQeoConnection->PayloadKey));

        C_ASSERT(sizeof(NdisConnection->HeaderKey) >= sizeof(XdpQeoConnection->HeaderKey));
        RtlCopyMemory(
            NdisConnection->HeaderKey, XdpQeoConnection->HeaderKey,
            sizeof(XdpQeoConnection->HeaderKey));

        C_ASSERT(sizeof(NdisConnection->PayloadIv) >= sizeof(XdpQeoConnection->PayloadIv));
        RtlCopyMemory(
            NdisConnection->PayloadIv, XdpQeoConnection->PayloadIv,
            sizeof(XdpQeoConnection->PayloadIv));

        NdisConnection->Status = NDIS_STATUS_PENDING;

        //
        // Advance to the next input connection.
        //
        XdpQeoConnection = RTL_PTR_ADD(XdpQeoConnection, XdpQeoConnection->Header.Size);
    }

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_DIRECT, NdisRequestMethod,
            OID_QUIC_CONNECTION_ENCRYPTION, NdisConnections, NdisConnectionsSize,
            NdisConnectionsSize, 0, &NdisBytesReturned);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Failed OID_QUIC_CONNECTION_ENCRYPTION Status=%!STATUS!",
            OffloadContext, Status);
        goto Exit;
    }

    if (!NT_VERIFY(NdisBytesReturned == NdisConnectionsSize)) {
        TraceError(
            TRACE_LWF,
            "OffloadContext=%p OID_QUIC_CONNECTION_ENCRYPTION unexpected NdisBytesReturned=%u",
            OffloadContext, NdisBytesReturned);
        Status = STATUS_DEVICE_DATA_ERROR;
        goto Exit;
    }

    //
    // Initialize the XDP output buffer with the data from the input buffer
    // before updating each connection's offload status code.
    //
    RtlMoveMemory(OffloadResult, XdpQeoParams->Connections, XdpQeoParams->ConnectionsSize);

    //
    // Use the immutable, validated input buffer to walk the output array: the
    // output buffer may be writable by user mode, so we treat it as strictly
    // write-only.
    //
    XdpQeoConnection = XdpQeoParams->Connections;

    for (UINT32 i = 0; i < XdpQeoParams->ConnectionCount; i++) {
        NDIS_QUIC_CONNECTION *NdisConnection = &NdisConnections[i];
        XDP_QUIC_CONNECTION *XdpQeoConnectionResult =
            RTL_PTR_ADD(OffloadResult,
                RTL_PTR_SUBTRACT(XdpQeoConnection, XdpQeoParams->Connections));

        XdpQeoConnectionResult->Status =
            HRESULT_FROM_WIN32(RtlNtStatusToDosErrorNoTeb(NdisConnection->Status));

        XdpQeoConnection = RTL_PTR_ADD(XdpQeoConnection, XdpQeoConnection->Header.Size);
    }

    *OffloadResultWritten = XdpQeoParams->ConnectionsSize;

Exit:

    if (NdisConnections != NULL) {
        ExFreePoolWithTag(NdisConnections, POOLTAG_OFFLOAD);
    }

    if (RundownAcquired) {
        ExReleaseRundownProtection(&Filter->Offload.FilterRundown);
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}
