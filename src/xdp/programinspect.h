//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#pragma warning(push)
#pragma warning(disable:4200) // nonstandard extension used: zero-sized array in struct/union

#pragma pack(push)
#pragma pack(1)
typedef struct QUIC_HEADER_INVARIANT {
    union {
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
        } COMMON_HDR;
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
            UINT32 Version;
            UCHAR DestCidLength;
            UCHAR DestCid[0];
            //UCHAR SourceCidLength;
            //UCHAR SourceCid[SourceCidLength];
        } LONG_HDR;
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
            UCHAR DestCid[0];
        } SHORT_HDR;
    };
} QUIC_HEADER_INVARIANT;
#pragma pack(pop)

typedef struct _XDP_PROGRAM_FRAME_STORAGE {
    ETHERNET_HEADER EthHdr;
    union {
        IPV4_HEADER Ip4Hdr;
        IPV6_HEADER Ip6Hdr;
    };
    union {
        UDP_HDR UdpHdr;
        TCP_HDR TcpHdr;
    };
    UINT8 TcpHdrOptions[40]; // Up to 40B options/paddings
    // Invariant header + 1 for SourceCidLength + 2x CIDS
    UINT8 QuicStorage[
        sizeof(QUIC_HEADER_INVARIANT) +
        sizeof(UCHAR) +
        XDP_QUIC_MAX_CID_LENGTH * 2];
} XDP_PROGRAM_FRAME_STORAGE;

typedef struct _XDP_PROGRAM_PAYLOAD_CACHE {
    XDP_BUFFER *Buffer;
    UINT32 BufferDataOffset;
    UINT32 FragmentIndex;
    UINT32 FragmentCount;
    BOOLEAN IsFragmentedBuffer;
} XDP_PROGRAM_PAYLOAD_CACHE;

typedef struct _XDP_PROGRAM_FRAME_CACHE {
    union {
        struct {
            UINT32 EthCached : 1;
            UINT32 EthValid : 1;
            UINT32 Ip4Cached : 1;
            UINT32 Ip4Valid : 1;
            UINT32 Ip6Cached : 1;
            UINT32 Ip6Valid : 1;
            UINT32 UdpCached : 1;
            UINT32 UdpValid : 1;
            UINT32 TcpCached : 1;
            UINT32 TcpValid : 1;
            UINT32 TransportPayloadCached : 1;
            UINT32 TransportPayloadValid : 1;
            UINT32 QuicCached : 1;
            UINT32 QuicValid : 1;
            UINT32 QuicIsLongHeader : 1;
        };
        UINT32 Flags;
    };

    ETHERNET_HEADER *EthHdr;
    union {
        IPV4_HEADER *Ip4Hdr;
        IPV6_HEADER *Ip6Hdr;
    };
    union {
        UDP_HDR *UdpHdr;
        TCP_HDR *TcpHdr;
    };
    UINT8 *TcpHdrOptions;
    UINT8 QuicCidLength;
    CONST UINT8 *QuicCid; // Src CID for long header, Dest CID for short header
    XDP_PROGRAM_PAYLOAD_CACHE TransportPayload;
} XDP_PROGRAM_FRAME_CACHE;

#pragma warning(push)
#pragma warning(disable:4324) // structure was padded due to alignment specifier

typedef struct _XDP_PROGRAM {
    //
    // Storage for discontiguous headers.
    //
    XDP_PROGRAM_FRAME_STORAGE FrameStorage;

    DECLSPEC_CACHEALIGN
    UINT32 RuleCount;
    XDP_RULE Rules[0];
} XDP_PROGRAM;

#pragma warning(pop)

#pragma warning(pop)

VOID
XdpProgramDeleteRule(
    _Inout_ XDP_RULE *Rule
    );

NTSTATUS
XdpProgramValidateRule(
    _Out_ XDP_RULE *ValidatedRule,
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ const XDP_RULE *UserRule,
    _In_ UINT32 RuleCount,
    _In_ UINT32 RuleIndex
    );

VOID
XdpProgramReleasePortSet(
    _Inout_ XDP_PORT_SET *PortSet
    );

NTSTATUS
XdpProgramCapturePortSet(
    _In_ CONST XDP_PORT_SET *UserPortSet,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Inout_ XDP_PORT_SET *KernelPortSet
    );
