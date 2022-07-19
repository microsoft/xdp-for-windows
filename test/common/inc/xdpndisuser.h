//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <xdpfnoid.h>

#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union

#define OID_GEN_RECEIVE_BLOCK_SIZE              0x0001010B
#define OID_GEN_CURRENT_PACKET_FILTER           0x0001010E
#define OID_GEN_RECEIVE_SCALE_PARAMETERS        0x00010204  // query and set
#define OID_TCP_OFFLOAD_CURRENT_CONFIG          0xFC01020B  // query only, handled by NDIS
#define OID_TCP_OFFLOAD_PARAMETERS              0xFC01020C  // set only
#define OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES   0xFC01020D  // query only


typedef ULONG NDIS_OID, *PNDIS_OID;

typedef enum _NDIS_REQUEST_TYPE
{
    NdisRequestQueryInformation,
    NdisRequestSetInformation,
    NdisRequestQueryStatistics,
    NdisRequestMethod = 12,
} NDIS_REQUEST_TYPE, *PNDIS_REQUEST_TYPE;

//
// NDIS Object Types used in NDIS_OBJECT_HEADER
//
#define NDIS_OBJECT_TYPE_DEFAULT                            0x80    // used when object type is implicit in the API call
#define NDIS_OBJECT_TYPE_RSS_PARAMETERS                     0x89    // used by miniport and protocol in NDIS_RECEIVE_SCALE_PARAMETERS
#define NDIS_OBJECT_TYPE_OFFLOAD                            0xA7

typedef struct _NDIS_OBJECT_HEADER
{
    UCHAR
        Type;

    UCHAR
        Revision;

    USHORT
        Size;

} NDIS_OBJECT_HEADER, *PNDIS_OBJECT_HEADER;

//
// Flags to denote the parameters that are kept unmodified.
//
#define NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED              0x0001
#define NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED             0x0002
#define NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED                0x0004
#define NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED              0x0008
#define NDIS_RSS_PARAM_FLAG_DISABLE_RSS                     0x0010
#define NDIS_RSS_PARAM_FLAG_DEFAULT_PROCESSOR_UNCHANGED     0x0020

#define NDIS_RSS_INDIRECTION_TABLE_SIZE_REVISION_1          128
#define NDIS_RSS_HASH_SECRET_KEY_SIZE_REVISION_1            40

//
// used in OID_GEN_RECEIVE_SCALE_PARAMETERS
//
#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_1     1

#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2     2

#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_3     3


typedef struct _NDIS_RECEIVE_SCALE_PARAMETERS
{
    NDIS_OBJECT_HEADER      Header;

    // Qualifies the rest of the information.
    USHORT                  Flags;

    // The base CPU number to do receive processing. not used.
    USHORT                  BaseCpuNumber;

    // This describes the hash function and type being enabled.
    ULONG                   HashInformation;

    // The size of indirection table array.
    USHORT                  IndirectionTableSize;
    // The offset of the indirection table from the beginning of this structure.
    ULONG                   IndirectionTableOffset;

    // The size of the secret key.
    USHORT                  HashSecretKeySize;
    // The offset of the secret key from the beginning of this structure.
    ULONG                   HashSecretKeyOffset;

    ULONG                   ProcessorMasksOffset;     //
    ULONG                   NumberOfProcessorMasks;   // Array of type GROUP_AFFINITY representing procs used in the indirection table
    ULONG                   ProcessorMasksEntrySize;  //

    // The hash map table is a CCHAR array for Revision 1.
    // It is a PROCESSOR_NUMBER array for Revision 2

    // Specifies default RSS processor.
    PROCESSOR_NUMBER        DefaultProcessorNumber;
} NDIS_RECEIVE_SCALE_PARAMETERS, *PNDIS_RECEIVE_SCALE_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, HashSecretKeyOffset)

//
// Maximum indirection table and private key sizes for revision 1
//
#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_1      128
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_1        40

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, ProcessorMasksEntrySize)

//
// Maximum indirection table and private key sizes for revision 2
//
#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2      (128*sizeof(PROCESSOR_NUMBER))
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2        40

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_3    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, DefaultProcessorNumber)

//
// Maximum indirection table and private key sizes for revision 3
//
#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_3      (128*sizeof(PROCESSOR_NUMBER))
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_3        40

#define NdisHashFunctionToeplitz                0x00000001 // the primary RSS hash function
#define NdisHashFunctionReserved1               0x00000002 // supported hash function 2
#define NdisHashFunctionReserved2               0x00000004 // supported hash function 3
#define NdisHashFunctionReserved3               0x00000008 // supported hash function 4

#define NDIS_HASH_FUNCTION_MASK                 0x000000FF
#define NDIS_HASH_TYPE_MASK                     0x00FFFF00

//
// What kind of hash field type the protocol asks the miniport to do
//
#define NDIS_HASH_IPV4                          0x00000100
#define NDIS_HASH_TCP_IPV4                      0x00000200
#define NDIS_HASH_IPV6                          0x00000400
#define NDIS_HASH_IPV6_EX                       0x00000800
#define NDIS_HASH_TCP_IPV6                      0x00001000
#define NDIS_HASH_TCP_IPV6_EX                   0x00002000
#define NDIS_HASH_UDP_IPV4                      0x00004000
#define NDIS_HASH_UDP_IPV6                      0x00008000
#define NDIS_HASH_UDP_IPV6_EX                   0x00010000

//
// Typedef to use as flags holder to correlate to the NDIS_HAS_ prefixed flags above.
//
typedef ULONG NDIS_HASH_FLAGS;

inline
ULONG
NDIS_RSS_HASH_FUNC_FROM_HASH_INFO(
    _In_ ULONG                                  HashInfo
    )
{
    return HashInfo & NDIS_HASH_FUNCTION_MASK;
}

inline
ULONG
NDIS_RSS_HASH_TYPE_FROM_HASH_INFO(
    _In_ ULONG                                  HashInfo
    )
{
    return HashInfo & NDIS_HASH_TYPE_MASK;
}

inline
ULONG
NDIS_RSS_HASH_INFO_FROM_TYPE_AND_FUNC(
    _In_ ULONG                                  HashType,
    _In_ ULONG                                  HashFunction
    )
{
    return HashType | HashFunction;
}

typedef _Return_type_success_(return >= 0) int NDIS_STATUS, *PNDIS_STATUS; // note default size

//
// offload specific status indication codes
//
#define NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG         ((NDIS_STATUS)0x40020006L)
#define NDIS_STATUS_TASK_OFFLOAD_HARDWARE_CAPABILITIES  ((NDIS_STATUS)0x40020007L)

//
// These defines and structures are used with
// OID_TCP_OFFLOAD_PARAMETERS
//

#define NDIS_OFFLOAD_PARAMETERS_NO_CHANGE                  0

//
// values used in IPv4Checksum, TCPIPv4Checksum, UDPIPv4Checksum
// TCPIPv6Checksum and UDPIPv6Checksum
//
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED     2
#define NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED     3
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED              4

//
// values used in LsoV1
//
#define NDIS_OFFLOAD_PARAMETERS_LSOV1_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED              2

//
// values used in IPsecV1
//
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_AH_ENABLED           2
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_ESP_ENABLED          3
#define NDIS_OFFLOAD_PARAMETERS_IPSECV1_AH_AND_ESP_ENABLED   4

//
// values used in LsoV2
//
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED              2

//
// values used in IPsecV2 and IPsecV2IPv4
//
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_AH_ENABLED           2
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_ESP_ENABLED          3
#define NDIS_OFFLOAD_PARAMETERS_IPSECV2_AH_AND_ESP_ENABLED   4
#define NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED             1
#define NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED              2

//
// Flags used in EncapsulationTypes field of NDIS_OFFLOAD_PARAMETERS
//
#define NDIS_ENCAPSULATION_TYPE_GRE_MAC          0x00000001

#define NDIS_ENCAPSULATION_TYPE_VXLAN            0x00000002

//
// values used in TcpConnectionIPv4 and TcpConnectionIPv6 fields
// of NDIS_OFFLOAD_PARAMETERS
//
#define NDIS_OFFLOAD_PARAMETERS_CONNECTION_OFFLOAD_DISABLED     1
#define NDIS_OFFLOAD_PARAMETERS_CONNECTION_OFFLOAD_ENABLED      2

//
// values used in UDP segmentation offload
//
#define NDIS_OFFLOAD_PARAMETERS_USO_DISABLED            1
#define NDIS_OFFLOAD_PARAMETERS_USO_ENABLED             2

//
// Used in OID_TCP_OFFLOAD_PARAMETERS for setting
// the offload parameters of a NIC
//

#define NDIS_OFFLOAD_PARAMETERS_REVISION_1            1

#define NDIS_OFFLOAD_PARAMETERS_REVISION_2            2

#define NDIS_OFFLOAD_PARAMETERS_REVISION_3            3

#define NDIS_OFFLOAD_PARAMETERS_REVISION_4            4

#define NDIS_OFFLOAD_PARAMETERS_REVISION_5            5

typedef struct _NDIS_OFFLOAD_PARAMETERS
{
    NDIS_OBJECT_HEADER      Header;

    UCHAR                   IPv4Checksum;
    UCHAR                   TCPIPv4Checksum;
    UCHAR                   UDPIPv4Checksum;

    UCHAR                   TCPIPv6Checksum;
    UCHAR                   UDPIPv6Checksum;

    UCHAR                   LsoV1;
    UCHAR                   IPsecV1;

    UCHAR                   LsoV2IPv4;
    UCHAR                   LsoV2IPv6;

    UCHAR                   TcpConnectionIPv4;
    UCHAR                   TcpConnectionIPv6;

    ULONG                   Flags;

    UCHAR                   IPsecV2;
    UCHAR                   IPsecV2IPv4;

    struct
    {
        UCHAR               RscIPv4;
        UCHAR               RscIPv6;
    };
    struct
    {
        UCHAR               EncapsulatedPacketTaskOffload;
        UCHAR               EncapsulationTypes;
    };

    union _ENCAPSULATION_PROTOCOL_PARAMETERS {
        struct _VXLAN_PARAMETERS {
            USHORT VxlanUDPPortNumber;
        } VxlanParameters;

        ULONG Value;

    } EncapsulationProtocolParameters;

    struct
    {
        UCHAR               IPv4;
        UCHAR               IPv6;
    } UdpSegmentation;
} NDIS_OFFLOAD_PARAMETERS, *PNDIS_OFFLOAD_PARAMETERS;

#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_1 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, Flags)

#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_2 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, IPsecV2IPv4)

#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_3 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, EncapsulationTypes)

#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_4 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, EncapsulationProtocolParameters)

#define NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5 RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD_PARAMETERS, UdpSegmentation)

#define NDIS_OFFLOAD_NOT_SUPPORTED              0
#define NDIS_OFFLOAD_SUPPORTED                  1

#define NDIS_OFFLOAD_SET_NO_CHANGE              0
#define NDIS_OFFLOAD_SET_ON                     1
#define NDIS_OFFLOAD_SET_OFF                    2

//
// Encapsulation types that are used during offload in query and set
//
#define NDIS_ENCAPSULATION_NOT_SUPPORTED        0x00000000
#define NDIS_ENCAPSULATION_NULL                 0x00000001
#define NDIS_ENCAPSULATION_IEEE_802_3           0x00000002
#define NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q   0x00000004
#define NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q_IN_OOB 0x00000008
#define NDIS_ENCAPSULATION_IEEE_LLC_SNAP_ROUTED 0x00000010

#pragma warning(push)
#pragma warning(disable:4214) //nonstandard extension used : bit field types other than int

//
// Describes the large send offload version 1 capabilities
// or configuration of the NIC. Used in NDIS_OFFLOAD structure
//
typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_V1
{

    struct
    {
        ULONG     Encapsulation;
        ULONG     MaxOffLoadSize;
        ULONG     MinSegmentCount;
        ULONG     TcpOptions:2;
        ULONG     IpOptions:2;
    } IPv4;

} NDIS_TCP_LARGE_SEND_OFFLOAD_V1, *PNDIS_TCP_LARGE_SEND_OFFLOAD_V1;

//
// Describes the checksum task offload capabilities or configuration
// of the NIC. used in NDIS_OFFLOAD structure
//
typedef struct _NDIS_TCP_IP_CHECKSUM_OFFLOAD
{

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpOptionsSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;
        ULONG       IpChecksum:2;
    } IPv4Transmit;

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpOptionsSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;
        ULONG       IpChecksum:2;
    } IPv4Receive;


    struct
    {
        ULONG       Encapsulation;
        ULONG       IpExtensionHeadersSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;

    } IPv6Transmit;

    struct
    {
        ULONG       Encapsulation;
        ULONG       IpExtensionHeadersSupported:2;
        ULONG       TcpOptionsSupported:2;
        ULONG       TcpChecksum:2;
        ULONG       UdpChecksum:2;

    } IPv6Receive;

} NDIS_TCP_IP_CHECKSUM_OFFLOAD, *PNDIS_TCP_IP_CHECKSUM_OFFLOAD;


//
// Describes the IPsec task offload version 1 capabilities
// or configuration of the NIC. Used in NDIS_OFFLOAD structure
//
typedef struct _NDIS_IPSEC_OFFLOAD_V1
{
    struct
    {
        ULONG   Encapsulation;
        ULONG   AhEspCombined;
        ULONG   TransportTunnelCombined;
        ULONG   IPv4Options;
        ULONG   Flags;
    } Supported;

    struct
    {
        ULONG   Md5:2;
        ULONG   Sha_1:2;
        ULONG   Transport:2;
        ULONG   Tunnel:2;
        ULONG   Send:2;
        ULONG   Receive:2;
    } IPv4AH;

    struct
    {
        ULONG   Des:2;
        ULONG   Reserved:2;
        ULONG   TripleDes:2;
        ULONG   NullEsp:2;
        ULONG   Transport:2;
        ULONG   Tunnel:2;
        ULONG   Send:2;
        ULONG   Receive:2;
    } IPv4ESP;

} NDIS_IPSEC_OFFLOAD_V1, *PNDIS_IPSEC_OFFLOAD_V1;

//
// Describes the large send offload version 2 capabilities
// or configuration of the NIC. Used in NDIS_OFFLOAD structure
//
typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_V2
{
    struct
    {
         ULONG     Encapsulation;
         ULONG     MaxOffLoadSize;
         ULONG     MinSegmentCount;
    }IPv4;

    struct
    {
         ULONG     Encapsulation;
         ULONG     MaxOffLoadSize;
         ULONG     MinSegmentCount;
         ULONG     IpExtensionHeadersSupported:2;
         ULONG     TcpOptionsSupported:2;
    }IPv6;

} NDIS_TCP_LARGE_SEND_OFFLOAD_V2, *PNDIS_TCP_LARGE_SEND_OFFLOAD_V2;

//
//  Structures for IPSec Task Offload V2.
//

//
// IPsec Algorithms for Authentication used in AuthenticationAlgorithms field
// of NDIS_IPSEC_OFFLOAD_V2 structure
//
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_MD5                      0x00000001
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_SHA_1                    0x00000002
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_SHA_256                  0x00000004
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_AES_GCM_128              0x00000008
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_AES_GCM_192              0x00000010
#define IPSEC_OFFLOAD_V2_AUTHENTICATION_AES_GCM_256              0x00000020

//
// IPsec Algorithms for Encryption used in EncryptionAlgorithms field of
// NDIS_IPSEC_OFFLOAD_V2 structure
//
#define IPSEC_OFFLOAD_V2_ENCRYPTION_NONE                          0x00000001
#define IPSEC_OFFLOAD_V2_ENCRYPTION_DES_CBC                       0x00000002
#define IPSEC_OFFLOAD_V2_ENCRYPTION_3_DES_CBC                     0x00000004
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_GCM_128                   0x00000008
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_GCM_192                   0x00000010
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_GCM_256                   0x00000020
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_CBC_128                   0x00000040
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_CBC_192                   0x00000080
#define IPSEC_OFFLOAD_V2_ENCRYPTION_AES_CBC_256                   0x00000100

//
// IPsec offload V2 capabilities used in  NDIS_OFFLOAD
//
typedef struct _NDIS_IPSEC_OFFLOAD_V2
{
    ULONG       Encapsulation;      // MAC encap types supported
    BOOLEAN     IPv6Supported;      // IPv6 Supported
    BOOLEAN     IPv4Options;                           // Supports offload of packets with IPv4 options
    BOOLEAN     IPv6NonIPsecExtensionHeaders;          // Supports offload of packets with non IPsec Extension headers
    BOOLEAN     Ah;
    BOOLEAN     Esp;
    BOOLEAN     AhEspCombined;
    BOOLEAN     Transport;
    BOOLEAN     Tunnel;
    BOOLEAN     TransportTunnelCombined;
    BOOLEAN     LsoSupported;
    BOOLEAN     ExtendedSequenceNumbers;
    ULONG       UdpEsp;
    ULONG       AuthenticationAlgorithms;     // Bit Mask of Authentication Algorithms
    ULONG       EncryptionAlgorithms;         // Bit Mask of Encryption Algorithms
    ULONG       SaOffloadCapacity;            // Number of SAs that can be offloaded
} NDIS_IPSEC_OFFLOAD_V2, *PNDIS_IPSEC_OFFLOAD_V2;

#pragma warning(pop)

typedef struct _NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD
{
    struct
    {
        BOOLEAN Enabled;
    } IPv4;
    struct
    {
        BOOLEAN Enabled;
    } IPv6;
} NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD, *PNDIS_TCP_RECV_SEG_COALESCE_OFFLOAD;

#define NDIS_TCP_RECV_SEG_COALESC_OFFLOAD_REVISION_1            1

#define NDIS_SIZEOF_TCP_RECV_SEG_COALESC_OFFLOAD_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD, IPv6.Enabled)

#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_NOT_SUPPORTED    0x00000000
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_INNER_IPV4       0x00000001
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_OUTER_IPV4       0x00000002
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_INNER_IPV6       0x00000004
#define NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_OUTER_IPV6       0x00000008

#pragma warning(push)
#pragma warning(disable:4214) //nonstandard extension used : bit field types other than int

typedef struct _NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD {
    ULONG TransmitChecksumOffloadSupported:4;
    ULONG ReceiveChecksumOffloadSupported:4;
    ULONG LsoV2Supported:4;
    ULONG RssSupported:4;
    ULONG VmqSupported:4;
    ULONG UsoSupported:4;
    ULONG Reserved:8;
    ULONG MaxHeaderSizeSupported;
} NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD, *PNDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD;

#pragma warning(pop)

#define NDIS_SIZEOF_ENCAPSULATED_PACKET_TASK_OFFLOAD_REVISION_1     \
        RTL_SIZEOF_THROUGH_FIELD(                                   \
            NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD, MaxHeaderSizeSupported)

typedef struct _NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2 {
    ULONG TransmitChecksumOffloadSupported:4;
    ULONG ReceiveChecksumOffloadSupported:4;
    ULONG LsoV2Supported:4;
    ULONG RssSupported:4;
    ULONG VmqSupported:4;
    ULONG UsoSupported:4;
    ULONG Reserved:8;
    ULONG MaxHeaderSizeSupported;

    union _ENCAPSULATION_PROTOCOL_INFO {
        struct _VXLAN_INFO {
           USHORT VxlanUDPPortNumber;
           USHORT VxlanUDPPortNumberConfigurable  :1;
        } VxlanInfo;

        ULONG Value;

    } EncapsulationProtocolInfo;

    ULONG Reserved1;
    ULONG Reserved2;
} NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2, *PNDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2;

typedef enum _NDIS_RFC6877_464XLAT_OFFLOAD_OPTIONS
{
    NDIS_RFC6877_464XLAT_OFFLOAD_NOT_SUPPORTED = 0,    // incapable of 464XLAT hardware offload
    NDIS_RFC6877_464XLAT_OFFLOAD_DISABLED,             // capable but disabled
    NDIS_RFC6877_464XLAT_OFFLOAD_ENABLED,              // capable and enabled all time
    NDIS_RFC6877_464XLAT_OFFLOAD_ON_DEMAND,            // capable and enabled only on-demand
} NDIS_RFC6877_464XLAT_OFFLOAD_OPTIONS;

typedef struct _NDIS_RFC6877_464XLAT_OFFLOAD
{
    NDIS_RFC6877_464XLAT_OFFLOAD_OPTIONS   XlatOffload;
    ULONG Flags;                                       // Reserved, always 0
} NDIS_RFC6877_464XLAT_OFFLOAD, *PNDIS_RFC6877_464XLAT_OFFLOAD;

typedef struct _NDIS_UDP_SEGMENTATION_OFFLOAD
{
    struct
    {
        ULONG     Encapsulation;
        ULONG     MaxOffLoadSize;
        ULONG     MinSegmentCount : 6;
        ULONG     SubMssFinalSegmentSupported : 1;
        ULONG     Reserved : 25;
    } IPv4;

    struct
    {
        ULONG     Encapsulation;
        ULONG     MaxOffLoadSize;
        ULONG     MinSegmentCount : 6;
        ULONG     SubMssFinalSegmentSupported : 1;
        ULONG     Reserved1 : 25;
        ULONG     IpExtensionHeadersSupported : 2;
        ULONG     Reserved2 : 30;
    } IPv6;
} NDIS_UDP_SEGMENTATION_OFFLOAD, *PNDIS_UDP_SEGMENTATION_OFFLOAD;

//
// flags used in Flags field of NDIS_OFFLOAD structure
//
#define NDIS_OFFLOAD_FLAGS_GROUP_CHECKSUM_CAPABILITIES  0x00000001

#define IPSEC_OFFLOAD_V2_AND_TCP_CHECKSUM_COEXISTENCE  0x00000002
#define IPSEC_OFFLOAD_V2_AND_UDP_CHECKSUM_COEXISTENCE  0x00000004

//
// Describes TCP/IP task offload capabilities or configuration
// of the NIC. Used in OID_TCP_OFFLOAD_CURRENT_CONFIG
// and OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES
//
#define NDIS_OFFLOAD_REVISION_1    1
#define NDIS_OFFLOAD_REVISION_2    2
#define NDIS_OFFLOAD_REVISION_3    3
#define NDIS_OFFLOAD_REVISION_4    4
#define NDIS_OFFLOAD_REVISION_5    5
#define NDIS_OFFLOAD_REVISION_6    6
#define NDIS_OFFLOAD_REVISION_7    7

typedef struct _NDIS_OFFLOAD
{
    NDIS_OBJECT_HEADER                       Header;

    //
    // Checksum Offload information
    //
    NDIS_TCP_IP_CHECKSUM_OFFLOAD             Checksum;

    //
    // Large Send Offload information
    //
    NDIS_TCP_LARGE_SEND_OFFLOAD_V1           LsoV1;

    //
    // IPsec Offload Information
    //
    NDIS_IPSEC_OFFLOAD_V1                    IPsecV1;

    //
    // Large Send Offload version 2Information
    //
    NDIS_TCP_LARGE_SEND_OFFLOAD_V2           LsoV2;

    ULONG                                    Flags;

    //
    //IPsec offload V2
    //
    NDIS_IPSEC_OFFLOAD_V2                    IPsecV2;

    //
    // Receive Segment Coalescing information
    //
    NDIS_TCP_RECV_SEG_COALESCE_OFFLOAD       Rsc;

    //
    // NVGRE Encapsulated packet task offload information
    //
    NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD    EncapsulatedPacketTaskOffloadGre;

    //
    // VXLAN Encapsulated packet task offload information
    //
    NDIS_ENCAPSULATED_PACKET_TASK_OFFLOAD_V2 EncapsulatedPacketTaskOffloadVxlan;

    //
    // Enabled encapsulation types for Encapsulated packet task offload
    //
    UCHAR                                    EncapsulationTypes;

    //
    // 464XLAT hardward offload information.
    //
    NDIS_RFC6877_464XLAT_OFFLOAD             Rfc6877Xlat;


    //
    // UDP segmentation offload.
    //
    NDIS_UDP_SEGMENTATION_OFFLOAD            UdpSegmentation;

} NDIS_OFFLOAD, *PNDIS_OFFLOAD;

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, Flags)

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_2   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, IPsecV2)

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_3   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, EncapsulatedPacketTaskOffloadGre)

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_4   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, EncapsulationTypes)

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_5   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, Rfc6877Xlat)

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_6   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, UdpSegmentation)

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_7   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, UdpSegmentation)

#pragma warning(pop)

#ifdef __cplusplus
} // extern "C"
#endif
