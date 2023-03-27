//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// The following definitions are proposed to be upstreamed:
// https://github.com/microsoft/ebpf-for-windows/issues/2145
//

#define EBPF_PROGRAM_TYPE_XDP_INIT { \
    0xf1832a85, \
    0x85d5, \
    0x45b0, \
    {0x98, 0xa0, 0x70, 0x69, 0xd6, 0x30, 0x13, 0xb0}}

#define EBPF_ATTACH_TYPE_XDP_INIT { \
    0x85e0d8ef, \
    0x579e, \
    0x4931, \
    {0xb0, 0x72, 0x8e, 0xe2, 0x26, 0xbb, 0x2e, 0x9d}}
