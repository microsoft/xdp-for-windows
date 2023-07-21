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
    XDP_MATCH_ALL,
    XDP_MATCH_UDP,
    XDP_MATCH_UDP_DST,
    XDP_MATCH_IPV4_DST_MASK,
    XDP_MATCH_IPV6_DST_MASK,
    XDP_MATCH_QUIC_FLOW_SRC_CID,
    XDP_MATCH_QUIC_FLOW_DST_CID,
    XDP_MATCH_IPV4_UDP_TUPLE,
    XDP_MATCH_IPV6_UDP_TUPLE,
    XDP_MATCH_UDP_PORT_SET,
    XDP_MATCH_IPV4_UDP_PORT_SET,
    XDP_MATCH_IPV6_UDP_PORT_SET,
    XDP_MATCH_IPV4_TCP_PORT_SET,
    XDP_MATCH_IPV6_TCP_PORT_SET,
    XDP_MATCH_TCP_DST,
    XDP_MATCH_TCP_QUIC_FLOW_SRC_CID,
    XDP_MATCH_TCP_QUIC_FLOW_DST_CID,
    XDP_MATCH_TCP_CONTROL_DST,
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

#define XDP_QUIC_MAX_CID_LENGTH 20

typedef struct _XDP_QUIC_FLOW {
    UINT16 UdpPort;
    UCHAR CidLength;
    UCHAR CidOffset;
    UCHAR CidData[XDP_QUIC_MAX_CID_LENGTH]; // Max allowed per QUIC v1 RFC
} XDP_QUIC_FLOW;

#define XDP_PORT_SET_BUFFER_SIZE ((MAXUINT16 + 1) / 8)

typedef struct _XDP_PORT_SET {
    const UINT8 *PortSet;
    VOID *Reserved;
} XDP_PORT_SET;

typedef struct _XDP_IP_PORT_SET {
    XDP_INET_ADDR Address;
    XDP_PORT_SET PortSet;
} XDP_IP_PORT_SET;

typedef union _XDP_MATCH_PATTERN {
    UINT16 Port;
    XDP_IP_ADDRESS_MASK IpMask;
    XDP_TUPLE Tuple;
    XDP_QUIC_FLOW QuicFlow;
    XDP_PORT_SET PortSet;
    XDP_IP_PORT_SET IpPortSet;
} XDP_MATCH_PATTERN;

typedef enum _XDP_RULE_ACTION {
    XDP_PROGRAM_ACTION_DROP,
    XDP_PROGRAM_ACTION_PASS,
    XDP_PROGRAM_ACTION_REDIRECT,
    XDP_PROGRAM_ACTION_L2FWD,
    //
    // Reserved.
    //
    XDP_PROGRAM_ACTION_EBPF,
} XDP_RULE_ACTION;

typedef enum _XDP_REDIRECT_TARGET_TYPE {
    XDP_REDIRECT_TARGET_TYPE_XSK,
} XDP_REDIRECT_TARGET_TYPE;

typedef struct _XDP_REDIRECT_PARAMS {
    XDP_REDIRECT_TARGET_TYPE TargetType;
    HANDLE Target;
} XDP_REDIRECT_PARAMS;

typedef struct _XDP_EBPF_PARAMS {
    HANDLE Target;
} XDP_EBPF_PARAMS;

typedef struct _XDP_RULE {
    XDP_MATCH_TYPE Match;
    XDP_MATCH_PATTERN Pattern;
    XDP_RULE_ACTION Action;
    union {
        XDP_REDIRECT_PARAMS Redirect;
        XDP_EBPF_PARAMS Ebpf;
    };
} XDP_RULE;

#pragma warning(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif
