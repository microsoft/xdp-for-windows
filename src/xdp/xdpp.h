//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Internal definitions for the XDP driver.
//

#pragma once

#define PAGED_ROUTINE __declspec(code_seg("PAGE"))

//
// Internal definitions.
//

#define XDP_POOLTAG_CPU_CONTEXT 'CpdX' // XdpC
#define XDP_POOLTAG_EXTENSION   'EpdX' // XdpE
#define XDP_POOLTAG_IF          'IpdX' // XdpI
#define XDP_POOLTAG_IFSET       'ipdX' // Xdpi
#define XDP_POOLTAG_INTERFACE   'fIdX' // XdIf
#define XDP_POOLTAG_MAP         'MpdX' // XdpM
#define XDP_POOLTAG_NMR         'NpdX' // XdpN
#define XDP_POOLTAG_PROGRAM     'PpdX' // XdpP
#define XDP_POOLTAG_RING        'rpdX' // Xdpr
#define XDP_POOLTAG_RXQUEUE     'RpdX' // XdpR
#define XDP_POOLTAG_TXQUEUE     'TpdX' // XdpT
