//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "offloadqeo.tmh"

static
NTSTATUS
XdpLwfOffloadQeoConvertXdpToNdis(
    _Out_ NDIS_QUIC_CONNECTION *NdisConnection,
    _In_ const XDP_QUIC_CONNECTION *XdpConnection
    )
{
    NTSTATUS Status;

    switch (XdpConnection->Operation) {
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

    switch (XdpConnection->Direction) {
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

    switch (XdpConnection->DecryptFailureAction) {
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

    NdisConnection->KeyPhase = XdpConnection->KeyPhase;

    switch (XdpConnection->CipherType) {
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

    switch (XdpConnection->AddressFamily) {
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

    NdisConnection->UdpPort = XdpConnection->UdpPort;
    NdisConnection->NextPacketNumber = XdpConnection->NextPacketNumber;
    NdisConnection->ConnectionIdLength = XdpConnection->ConnectionIdLength;

    C_ASSERT(sizeof(NdisConnection->Address) >= sizeof(XdpConnection->Address));
    RtlCopyMemory(
        NdisConnection->Address, XdpConnection->Address,
        sizeof(XdpConnection->Address));

    C_ASSERT(sizeof(NdisConnection->ConnectionId) >= sizeof(XdpConnection->ConnectionId));
    RtlCopyMemory(
        NdisConnection->ConnectionId, XdpConnection->ConnectionId,
        sizeof(XdpConnection->ConnectionId));

    C_ASSERT(sizeof(NdisConnection->PayloadKey) >= sizeof(XdpConnection->PayloadKey));
    RtlCopyMemory(
        NdisConnection->PayloadKey, XdpConnection->PayloadKey,
        sizeof(XdpConnection->PayloadKey));

    C_ASSERT(sizeof(NdisConnection->HeaderKey) >= sizeof(XdpConnection->HeaderKey));
    RtlCopyMemory(
        NdisConnection->HeaderKey, XdpConnection->HeaderKey,
        sizeof(XdpConnection->HeaderKey));

    C_ASSERT(sizeof(NdisConnection->PayloadIv) >= sizeof(XdpConnection->PayloadIv));
    RtlCopyMemory(
        NdisConnection->PayloadIv, XdpConnection->PayloadIv,
        sizeof(XdpConnection->PayloadIv));

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

NTSTATUS
XdpLwfOffloadQeoSet(
    _In_ XDP_LWF_FILTER *Filter,
    _In_ XDP_LWF_INTERFACE_OFFLOAD_CONTEXT *OffloadContext,
    _In_ const XDP_OFFLOAD_PARAMS_QEO *XdpQeoParams,
    _In_ UINT32 XdpQeoParamsSize
    )
{
    NTSTATUS Status;
    LIST_ENTRY *Entry;
    NDIS_OID Oid;
    NDIS_QUIC_CONNECTION *NdisConnections = NULL;
    UINT32 NdisConnectionsSize;
    ULONG NdisBytesReturned;

    TraceEnter(TRACE_LWF, "Filter=%p OffloadContext=%p", Filter, OffloadContext);

    if (XdpQeoParamsSize != sizeof(*XdpQeoParams) ||
        XdpQeoParams->ConnectionCount == 0) {
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

    Entry = XdpQeoParams->Connections.Flink;

    for (UINT32 i = 0; Entry != &XdpQeoParams->Connections; i++) {
        XDP_OFFLOAD_PARAMS_QEO_CONNECTION *XdpQeoConnection =
            CONTAINING_RECORD(Entry, XDP_OFFLOAD_PARAMS_QEO_CONNECTION, TransactionEntry);
        Entry = Entry->Flink;
        NDIS_QUIC_CONNECTION *NdisConnection = &NdisConnections[i];

        Status = XdpLwfOffloadQeoConvertXdpToNdis(NdisConnection, &XdpQeoConnection->Params);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        NdisConnection->Status = NDIS_STATUS_PENDING;
    }

    Oid = OID_QUIC_CONNECTION_ENCRYPTION;

RetryWithPrototypeOid:

    Status =
        XdpLwfOidInternalRequest(
            Filter->NdisFilterHandle, XDP_OID_REQUEST_INTERFACE_DIRECT, NdisRequestMethod, Oid,
            NdisConnections, NdisConnectionsSize, NdisConnectionsSize, 0, &NdisBytesReturned);
    if (!NT_SUCCESS(Status)) {
        if (NdisGetVersion() <= NDIS_RUNTIME_VERSION_688 &&
            Status == NDIS_STATUS_NOT_SUPPORTED &&
            Oid == OID_QUIC_CONNECTION_ENCRYPTION) {
            //
            // As a temporary workaround for a lack of support for the QEO OID
            // in older versions of Windows, retry the request using a
            // repurposed IPSec OID. This workaround should be removed as soon
            // as possible.
            //
            Oid = OID_QUIC_CONNECTION_ENCRYPTION_PROTOTYPE;
            goto RetryWithPrototypeOid;
        }

        TraceError(
            TRACE_LWF,
            "OffloadContext=%p Failed OID=%x Status=%!STATUS!",
            OffloadContext, Oid, Status);
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

    Entry = XdpQeoParams->Connections.Flink;

    for (UINT32 i = 0; Entry != &XdpQeoParams->Connections; i++) {
        XDP_OFFLOAD_PARAMS_QEO_CONNECTION *XdpQeoConnection =
            CONTAINING_RECORD(Entry, XDP_OFFLOAD_PARAMS_QEO_CONNECTION, TransactionEntry);
        Entry = Entry->Flink;
        NDIS_QUIC_CONNECTION *NdisConnection = &NdisConnections[i];

        XdpQeoConnection->Params.Status =
            HRESULT_FROM_WIN32(RtlNtStatusToDosErrorNoTeb(XdpConvertNdisStatusToNtStatus(
                NdisConnection->Status)));
    }

Exit:

    if (NdisConnections != NULL) {
        ExFreePoolWithTag(NdisConnections, POOLTAG_OFFLOAD);
    }

    TraceExitStatus(TRACE_LWF);

    return Status;
}
