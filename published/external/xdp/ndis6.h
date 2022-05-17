//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

//
// Redefine NDIS6 OID in XDP headers until an updated DDK is released.
//

//
// An XDP-capable NDIS6 interface must respond to OID_XDP_QUERY_CAPABILITIES by
// providing the interface's registered XDP_CAPABILITIES structure. This OID is
// an NDIS query OID.
//
#ifndef OID_XDP_QUERY_CAPABILITIES
#define OID_XDP_QUERY_CAPABILITIES  0x00A00204
#endif

EXTERN_C_END
