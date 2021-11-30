//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

FILTER_OID_REQUEST FilterOidRequest;
FILTER_OID_REQUEST_COMPLETE FilterOidRequestComplete;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
OidIrpSubmitRequest(
    _In_ LWF_FILTER *Filter,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
