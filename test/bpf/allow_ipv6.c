//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "bpf_endian.h"
#include "bpf_helpers.h"
#include "net/if_ether.h"
#include "net/ip.h"

SEC("xdp/allow_ipv6")
int
allow_ipv6(xdp_md_t *XdpMd)
{
    ETHERNET_HEADER *Ethernet;
    IPV6_HEADER *Ipv6;
    const int L2L3HeaderSize = sizeof(*Ethernet) + sizeof(*Ipv6);
    int XdpAction = XDP_DROP;

    //
    // Allow IPv6 traffic and drop everything else.
    //

    if ((char *)XdpMd->data + L2L3HeaderSize > (char *)XdpMd->data_end) {
        goto Exit;
    }

    Ethernet = (ETHERNET_HEADER *)XdpMd->data;
    if (Ethernet->Type != htons(ETHERNET_TYPE_IPV6)) {
        goto Exit;
    }

    Ipv6 = (IPV6_HEADER *)(Ethernet + 1);
    if (Ipv6->Version != 6) {
        goto Exit;
    }
    if (ntohs(Ipv6->PayloadLength) !=
        ((char *)XdpMd->data_end - (char *)XdpMd->data) - L2L3HeaderSize) {
        goto Exit;
    }

    // Looks like IPv6!
    XdpAction = XDP_PASS;

Exit:

    return XdpAction;
}
