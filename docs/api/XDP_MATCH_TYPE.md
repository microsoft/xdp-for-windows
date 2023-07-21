
# XDP_MATCH_TYPE structure

Specifies an XDP inspection program rule match type.

## Syntax

```C
typedef enum _XDP_MATCH_TYPE {
    //
    // Match all frames.
    //
    XDP_MATCH_ALL,
    //
    // Match all UDP frames.
    //
    XDP_MATCH_UDP,
    //
    // Match frames with a specific UDP port number as their destination port.
    // The port number is specified by field Port in XDP_MATCH_PATTERN.
    //
    XDP_MATCH_UDP_DST,
    //
    // Match IPv4 frames based on their destination address, using an IP address mask.
    // The address mask is specified by field IpMask in XDP_MATCH_PATTERN.
    //
    XDP_MATCH_IPV4_DST_MASK,
    //
    // Match IPv6 frames based on their destination address, using an IP address mask.
    // The address mask is specified by field IpMask in XDP_MATCH_PATTERN.
    //
    XDP_MATCH_IPV6_DST_MASK,
    //
    // Match UDP destination port and QUIC source connection IDs in long header
    // QUIC packets. The supplied buffer must match the CID at the given offset.
    //
    XDP_MATCH_QUIC_FLOW_SRC_CID,
    //
    // Match UDP destination port and QUIC destination connection IDs in short
    // header QUIC packets. The supplied buffer must match the CID at the given
    // offset.
    //
    XDP_MATCH_QUIC_FLOW_DST_CID,
    //
    // Match frames with a specific source and destination IPv4 addresses and UDP
    // port numbers.
    //
    XDP_MATCH_IPV4_UDP_TUPLE,
    //
    // Match frames with a specific source and destination IPv6 addresses and UDP
    // port numbers.
    //
    XDP_MATCH_IPV6_UDP_TUPLE,
    //
    // Match frames with a destination UDP port enabled in the port set.
    //
    XDP_MATCH_UDP_PORT_SET,
    //
    // Match IPv4 frames matching the destination address and the destination
    // UDP port enabled in the port set.
    //
    XDP_MATCH_IPV4_UDP_PORT_SET,
    //
    // Match IPv6 frames matching the destination address and the destination
    // UDP port enabled in the port set.
    //
    XDP_MATCH_IPV6_UDP_PORT_SET,
    //
    // Match IPv4 frames matching the destination address and the destination
    // TCP port enabled in the port set.
    //
    XDP_MATCH_IPV4_TCP_PORT_SET,
    //
    // Match IPv6 frames matching the destination address and the destination
    // TCP port enabled in the port set.
    //
    XDP_MATCH_IPV6_TCP_PORT_SET,
    //
    // Match frames with a specific TCP port number as their destination port.
    // The port number is specified by field Port in XDP_MATCH_PATTERN.
    //
    XDP_MATCH_TCP_DST,
    //
    // Match TCP destination port and QUIC source connection IDs in long header
    // QUIC packets. The supplied buffer must match the CID at the given offset.
    //
    XDP_MATCH_TCP_QUIC_FLOW_SRC_CID,
    //
    // Match TCP destination port and QUIC destination connection IDs in short
    // header QUIC packets. The supplied buffer must match the CID at the given
    // offset.
    //
    XDP_MATCH_TCP_QUIC_FLOW_DST_CID,
    //
    // Match frames with a specific TCP port number as their destination port and
    // TCP control flags (SYN, FIN and RST). The port number is specified by field
    // Port in XDP_MATCH_PATTERN.
    //
    XDP_MATCH_TCP_CONTROL_DST,
} XDP_MATCH_TYPE;
```

## Members

TODO

## Remarks

TODO
