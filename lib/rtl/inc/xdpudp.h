//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

typedef struct _UDP_HDR {
    UINT16 uh_sport;
    UINT16 uh_dport;
    UINT16 uh_ulen;
    UINT16 uh_sum;
} UDP_HDR;
