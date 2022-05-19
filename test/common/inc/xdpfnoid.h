//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This header contains private OIDs shared between the FNLWF and FNMP drivers.
// NDIS requires private OIDs set the highest 8 bits.
//

EXTERN_C_START

//
// Set hardware offload capabilities.
//
#define OID_TCP_OFFLOAD_HW_PARAMETERS   0xff000000

EXTERN_C_END
