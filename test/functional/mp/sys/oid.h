//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

extern CONST NDIS_OID MpSupportedOidArray[];
extern CONST UINT32 MpSupportedOidArraySize;

MINIPORT_CANCEL_OID_REQUEST MiniportCancelRequestHandler;
MINIPORT_OID_REQUEST MiniportRequestHandler;
