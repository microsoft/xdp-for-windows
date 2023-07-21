//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpGuidCreate(
    _Out_ GUID *Guid
    );

EXTERN_C_END
