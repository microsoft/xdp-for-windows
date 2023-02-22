//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDPPROGRAM_H
#define XDPPROGRAM_H

#include <in6addr.h>
#include <inaddr.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union

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
} XDP_MATCH_TYPE;

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

#define QUIC_MAX_CID_LENGTH 20

typedef struct _XDP_QUIC_FLOW {
    UINT16 UdpPort;
    UCHAR CidLength;
    UCHAR CidOffset;
    UCHAR CidData[QUIC_MAX_CID_LENGTH]; // Max allowed per QUIC v1 RFC
} XDP_QUIC_FLOW;

#define XDP_PORT_SET_BUFFER_SIZE ((MAXUINT16 + 1) / 8)

typedef struct _XDP_PORT_SET {
    //
    // A port is mapped to the N/8th byte and the N%8th bit. The underlying
    // buffer must be 8-byte aligned. The buffer size (in bytes) must be
    // XDP_PORT_SET_BUFFER_SIZE. The port is represented in network order.
    //
    UINT8 *PortSet;
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

typedef enum _XDP_RULE_ACTION {
    //
    // Frame must be dropped.
    //
    XDP_PROGRAM_ACTION_DROP,
    //
    // Frame must be allowed to continue.
    //
    XDP_PROGRAM_ACTION_PASS,
    //
    // Frame must be redirected to the target specified in XDP_REDIRECT_PARAMS.
    //
    XDP_PROGRAM_ACTION_REDIRECT,
    //
    // The frame's ethernet source and destination addresses are swapped and the
    // frame is directed onto the return path. For native XDP drivers, this
    // results in an XDP_RX_ACTION_TX.
    //
    XDP_PROGRAM_ACTION_L2FWD,
} XDP_RULE_ACTION;

//
// Target types for a redirect action.
//
typedef enum _XDP_REDIRECT_TARGET_TYPE {
    //
    // Redirect frames to an XDP socket.
    //
    XDP_REDIRECT_TARGET_TYPE_XSK,
} XDP_REDIRECT_TARGET_TYPE;

typedef struct _XDP_REDIRECT_PARAMS {
    XDP_REDIRECT_TARGET_TYPE TargetType;
    HANDLE Target;
} XDP_REDIRECT_PARAMS;

//
// XDP program rule.
//
typedef struct _XDP_RULE {
    XDP_MATCH_TYPE Match;
    XDP_MATCH_PATTERN Pattern;
    XDP_RULE_ACTION Action;
    union {
        XDP_REDIRECT_PARAMS Redirect;
    };
} XDP_RULE;

#pragma warning(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif
