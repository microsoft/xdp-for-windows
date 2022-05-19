//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

_IRQL_requires_max_(HIGH_LEVEL)
NDIS_STATUS
inline
XdpConvertNtStatusToNdisStatus(NTSTATUS NtStatus)
{
    if (NT_SUCCESS(NtStatus) &&
        NtStatus != STATUS_PENDING &&
        NtStatus != STATUS_NDIS_INDICATION_REQUIRED) {
        return NDIS_STATUS_SUCCESS;
    } else {
        switch (NtStatus) {
        case STATUS_BUFFER_TOO_SMALL:
            return NDIS_STATUS_BUFFER_TOO_SHORT;
        default:
            return (NDIS_STATUS)NtStatus;
        }
    }
}

_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
inline
XdpConvertNdisStatusToNtStatus(NDIS_STATUS NdisStatus)
{
    if (NT_SUCCESS(NdisStatus) &&
        NdisStatus != NDIS_STATUS_SUCCESS &&
        NdisStatus != NDIS_STATUS_PENDING &&
        NdisStatus != NDIS_STATUS_INDICATION_REQUIRED) {
        //
        // Case where an NDIS error is incorrectly mapped as a success by NT_SUCCESS macro
        //
        return STATUS_UNSUCCESSFUL;
    } else {
        switch (NdisStatus) {
        case NDIS_STATUS_BUFFER_TOO_SHORT:
            return STATUS_BUFFER_TOO_SMALL;
        default:
            return (NTSTATUS)NdisStatus;
        }
    }
}
