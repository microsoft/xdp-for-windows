//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _UDP_HDR {
    UINT16 uh_sport;
    UINT16 uh_dport;
    UINT16 uh_ulen;
    UINT16 uh_sum;
} UDP_HDR;
