# XDP_MATCH_PATTERN structure

Specifies an XDP inspection program rule match pattern.

```C
typedef union _XDP_INET_ADDR {
    IN_ADDR Ipv4;
    IN6_ADDR Ipv6;
} XDP_INET_ADDR;

typedef struct _XDP_IP_ADDRESS_MASK {
    XDP_INET_ADDR Mask;
    XDP_INET_ADDR Address;
} XDP_IP_ADDRESS_MASK;

typedef struct _XDP_TUPLE {
    XDP_INET_ADDR SourceAddress;
    XDP_INET_ADDR DestinationAddress;
    UINT16 SourcePort;
    UINT16 DestinationPort;
} XDP_TUPLE;

typedef struct _XDP_QUIC_FLOW {
    UINT16 UdpPort;
    UCHAR CidLength;
    UCHAR CidOffset;
    UCHAR CidData[XDP_QUIC_MAX_CID_LENGTH]; // Max allowed per QUIC v1 RFC
} XDP_QUIC_FLOW;

typedef struct _XDP_PORT_SET {
    //
    // A port is mapped to the N/8th byte and the N%8th bit. The underlying
    // buffer must be 8-byte aligned. The buffer size (in bytes) must be
    // XDP_PORT_SET_BUFFER_SIZE. The port is represented in network order.
    //
    const UINT8 *PortSet;
    VOID *Reserved;
} XDP_PORT_SET;

typedef struct _XDP_IP_PORT_SET {
    XDP_INET_ADDR Address;
    XDP_PORT_SET PortSet;
} XDP_IP_PORT_SET;

//
// Defines a pattern to match frames.
//
typedef union _XDP_MATCH_PATTERN {
    //
    // Match on port number.
    //
    UINT16 Port;
    //
    // Match on a partial IP address.
    // The bitwise AND operation is applied to:
    //    * the Mask field of XDP_IP_ADDRESS_MASK and
    //    * the IP address of the frame.
    // The result is compared to the Address field of XDP_IP_ADDRESS_MASK.
    //
    XDP_IP_ADDRESS_MASK IpMask;
    //
    // Match on source and destination IP addresses and ports.
    //
    XDP_TUPLE Tuple;
    //
    // Match on UDP port and QUIC connection ID.
    //
    XDP_QUIC_FLOW QuicFlow;
    //
    // Match on destination port.
    //
    XDP_PORT_SET PortSet;
    //
    // Match on destination IP address and port.
    //
    XDP_IP_PORT_SET IpPortSet;
} XDP_MATCH_PATTERN;
```

## Members

TODO

## Remarks

TODO
