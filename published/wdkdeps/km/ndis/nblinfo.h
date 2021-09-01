// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#pragma region System Family (kernel drivers) with Desktop Family for compat
#include <winapifamily.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)

#include <ndis/types.h>
#include <ndis/version.h>

EXTERN_C_START

typedef enum _NDIS_NET_BUFFER_LIST_INFO
{
    TcpIpChecksumNetBufferListInfo,
    TcpOffloadBytesTransferred = TcpIpChecksumNetBufferListInfo,
    IPsecOffloadV1NetBufferListInfo,
#if NDIS_SUPPORT_NDIS61
    IPsecOffloadV2NetBufferListInfo = IPsecOffloadV1NetBufferListInfo,
#endif // NDIS_SUPPORT_NDIS61
    TcpLargeSendNetBufferListInfo,
    TcpReceiveNoPush = TcpLargeSendNetBufferListInfo,
    ClassificationHandleNetBufferListInfo,
    Ieee8021QNetBufferListInfo,
    NetBufferListCancelId,
    MediaSpecificInformation,
    NetBufferListFrameType,
    NetBufferListProtocolId = NetBufferListFrameType,
    NetBufferListHashValue,
    NetBufferListHashInfo,
    WfpNetBufferListInfo,
#if NDIS_SUPPORT_NDIS61
    IPsecOffloadV2TunnelNetBufferListInfo,
    IPsecOffloadV2HeaderNetBufferListInfo,
#endif // NDIS_SUPPORT_NDIS61

#if NDIS_SUPPORT_NDIS620
    NetBufferListCorrelationId,
    NetBufferListFilteringInfo,

    MediaSpecificInformationEx,
    NblOriginalInterfaceIfIndex,
    NblReAuthWfpFlowContext = NblOriginalInterfaceIfIndex,
    TcpReceiveBytesTransferred,
    NrtNameResolutionId = TcpReceiveBytesTransferred,
#if NDIS_SUPPORT_NDIS684
    // @@internal In the tx path, this slot is used for NRT.
    // @@internal In the rx path, this slot was used for TCP chimney offload,
    // @@internal which is no longer supported. Re-purposing the rx path.
    UdpRecvSegCoalesceOffloadInfo = TcpReceiveBytesTransferred,
#endif // NDIS_SUPPORT_NDIS684

#if NDIS_SUPPORT_NDIS630

#if defined(_AMD64_) || defined(_ARM64_)
    //
    // This is a public header where the existing enumerations _CANNOT_ change.
    // The 32b compatible enumerations are listed separately below under
    // 32b compilation only.
    //
    SwitchForwardingReserved,
    SwitchForwardingDetail,
    VirtualSubnetInfo,
#endif // defined(_AMD64_) || defined(_ARM64_)

    IMReserved,
    TcpRecvSegCoalesceInfo,
#if NDIS_SUPPORT_NDIS683
    UdpSegmentationOffloadInfo = TcpRecvSegCoalesceInfo,
#endif // NDIS_SUPPORT_NDIS683
    RscTcpTimestampDelta,
    TcpSendOffloadsSupplementalNetBufferListInfo = RscTcpTimestampDelta,

#if NDIS_SUPPORT_NDIS650
#if defined(_AMD64_) || defined(_ARM64_)
    GftOffloadInformation,
    GftFlowEntryId,
#endif // defined(_AMD64_) || defined(_ARM64_)

#if NDIS_SUPPORT_NDIS680
#if _WIN64
    NetBufferListInfoReserved3,
#else
    NetBufferListInfoReserved3,
    NetBufferListInfoReserved4,
#endif // _WIN64
#endif // NDIS_SUPPORT_NDIS680

#endif // NDIS_SUPPORT_NDIS650

#endif // NDIS_SUPPORT_NDIS630

#endif // NDIS_SUPPORT_NDIS620

#if NDIS_SUPPORT_NDIS682
#if !defined(_AMD64_) && !defined(_ARM64_)
    //
    // 32b compilation only avaiable in NDIS682 and higher.  The enumeration
    // repetition here is necessary to preserve backwards compat with existing
    // ordering.
    //
    // The _NET_BUFFER_LIST->NetBufferListInfo[] (ie. NBL OOB) contract requires
    // that content be of pointer size. See details below.
    //
    //     SwitchForwardingReserved:
    //         - Fully 32b/64b compat.
    //         - Always used as a pointer in both set and get operations via
    //           VmsNblHelperGetPacketExt() and VmsNblHelperSetPacketExt(),
    //           using PVMS_PACKET_EXT_HEADER type.
    //
    //     SwitchForwardingDetail:
    //         - Not compliant with OOB contract since the contents are of
    //           _NDIS_SWITCH_FORWARDING_DETAIL_NET_BUFFER_LIST_INFO type which
    //           assumes a 64b OOB container.
    //         - This is addressed by using two members in
    //           _NET_BUFFER_LIST->NetBufferListInfo[] to allow for fitting the
    //           64b OOB data.
    //         - Access also occurs via NET_BUFFER_LIST_SWITCH_FORWARDING_DETAIL
    //           which requires 64b read.
    //         - Since this is only present in 32b, there's no backwards compat
    //           issue.
    //
    //     VirtualSubnetInfo:
    //         - Fully 32b/64b compat via preprocessor conditionals.
    //         - See _NDIS_NET_BUFFER_LIST_VIRTUAL_SUBNET_INFO for details.
    //
    SwitchForwardingReserved,
    SwitchForwardingDetail_b0_to_b31,
    SwitchForwardingDetail_b32_to_b63,
    VirtualSubnetInfo,
#endif // !defined(_AMD64_) && !defined(_ARM64_)
#endif // NDIS_SUPPORT_NDIS682

#if NDIS_WRAPPER == 1
    NetBufferListInfoReserved1,
    NetBufferListInfoReserved2,
#endif//NDIS_WRAPPER

    MaxNetBufferListInfo
} NDIS_NET_BUFFER_LIST_INFO, *PNDIS_NET_BUFFER_LIST_INFO;

EXTERN_C_END

#endif // WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_DESKTOP)
#pragma endregion

