//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This header declares experimental XDP interfaces. All definitions within
// this file are subject to breaking changes, including removal.
//

#ifndef XDPAPI_EXPERIMENTAL_H
#define XDPAPI_EXPERIMENTAL_H

#ifdef __cplusplus
extern "C" {
#endif

//
// RSS offload.
//

#define XDP_RSS_

#define XDP_RSS_HASH_TYPE_IPV4        0x001
#define XDP_RSS_HASH_TYPE_TCP_IPV4    0x002
#define XDP_RSS_HASH_TYPE_UDP_IPV4    0x004
#define XDP_RSS_HASH_TYPE_IPV6        0x008
#define XDP_RSS_HASH_TYPE_TCP_IPV6    0x010
#define XDP_RSS_HASH_TYPE_UDP_IPV6    0x020
#define XDP_RSS_HASH_TYPE_IPV6_EX     0x040
#define XDP_RSS_HASH_TYPE_TCP_IPV6_EX 0x080
#define XDP_RSS_HASH_TYPE_UDP_IPV6_EX 0x100
#define XDP_RSS_VALID_HASH_TYPES ( \
    XDP_RSS_HASH_TYPE_IPV4 | \
    XDP_RSS_HASH_TYPE_TCP_IPV4 | \
    XDP_RSS_HASH_TYPE_UDP_IPV4 | \
    XDP_RSS_HASH_TYPE_IPV6 | \
    XDP_RSS_HASH_TYPE_TCP_IPV6 | \
    XDP_RSS_HASH_TYPE_UDP_IPV6 | \
    XDP_RSS_HASH_TYPE_IPV6_EX | \
    XDP_RSS_HASH_TYPE_TCP_IPV6_EX | \
    XDP_RSS_HASH_TYPE_UDP_IPV6_EX | \
    0)

typedef struct _XDP_RSS_CAPABILITIES {
    XDP_OBJECT_HEADER Header;
    UINT32 Flags;

    //
    // Supported hash types. Contains OR'd XDP_RSS_HASH_TYPE_* flags, or 0 to
    // indicate RSS is not supported.
    //
    UINT32 HashTypes;

    //
    // Maximum hash secret key size, in bytes.
    //
    UINT32 HashSecretKeySize;

    //
    // Number of hardware receive queues.
    //
    UINT32 NumberOfReceiveQueues;

    //
    // Maximum number of indirection table entries.
    //
    UINT32 NumberOfIndirectionTableEntries;
} XDP_RSS_CAPABILITIES;

#define XDP_RSS_CAPABILITIES_REVISION_1 1

#define XDP_SIZEOF_RSS_CAPABILITIES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RSS_CAPABILITIES, NumberOfReceiveQueues)

#define XDP_RSS_CAPABILITIES_REVISION_2 2

#define XDP_SIZEOF_RSS_CAPABILITIES_REVISION_2 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RSS_CAPABILITIES, NumberOfIndirectionTableEntries)

//
// Initializes an RSS capabilities object.
//
inline
VOID
XdpInitializeRssCapabilities(
    _Out_ XDP_RSS_CAPABILITIES *RssCapabilities
    )
{
    RtlZeroMemory(RssCapabilities, sizeof(*RssCapabilities));
    RssCapabilities->Header.Revision = XDP_RSS_CAPABILITIES_REVISION_1;
    RssCapabilities->Header.Size = XDP_SIZEOF_RSS_CAPABILITIES_REVISION_1;
}

//
// Query RSS capabilities on an interface. If the input RssCapabilitiesSize is
// too small, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) will be returned.
// Call with a NULL RssCapabilities to get the length.
//
typedef
HRESULT
XDP_RSS_GET_CAPABILITIES_FN(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    );

#define XDP_RSS_GET_CAPABILITIES_FN_NAME "XdpRssGetCapabilitiesExperimental"

//
// Upon set, indicates XDP_RSS_CONFIGURATION.HashType should not be ignored.
//
#define XDP_RSS_FLAG_SET_HASH_TYPE         0x0001
//
// Upon set, indicates XDP_RSS_CONFIGURATION.HashSecretKeySize and
// XDP_RSS_CONFIGURATION.HashSecretKeyOffset should not be ignored.
//
#define XDP_RSS_FLAG_SET_HASH_SECRET_KEY   0x0002
//
// Upon set, indicates XDP_RSS_CONFIGURATION.IndirectionTableSize and
// XDP_RSS_CONFIGURATION.IndirectionTableOffset should not be ignored.
//
#define XDP_RSS_FLAG_SET_INDIRECTION_TABLE 0x0004
//
// Upon set, indicates RSS should be disabled.
// Upon get, indicates RSS is disabled.
//
#define XDP_RSS_FLAG_DISABLED              0x0008
#define XDP_RSS_VALID_FLAGS ( \
    XDP_RSS_FLAG_SET_HASH_TYPE | \
    XDP_RSS_FLAG_SET_HASH_SECRET_KEY | \
    XDP_RSS_FLAG_SET_INDIRECTION_TABLE | \
    XDP_RSS_FLAG_DISABLED | \
    0)

typedef struct _XDP_RSS_CONFIGURATION {
    XDP_OBJECT_HEADER Header;

    UINT32 Flags;

    //
    // Packet hash type.
    // Contains OR'd XDP_RSS_HASH_TYPE_* flags, or 0 to indicate RSS is disabled.
    //
    UINT32 HashType;

    //
    // Number of bytes from the start of this struct to the start of the hash
    // secret key.
    //
    UINT16 HashSecretKeyOffset;

    //
    // Number of valid bytes in the hash secret key. Hash secret key
    // representation is UCHAR[].
    //
    UINT16 HashSecretKeySize;

    //
    // Number of bytes from the start of this struct to the start of the
    // indirection table.
    //
    UINT16 IndirectionTableOffset;

    //
    // Number of valid bytes in the indirection table. Indirection table
    // representation is PROCESSOR_NUMBER[].
    //
    UINT16 IndirectionTableSize;
} XDP_RSS_CONFIGURATION;

#define XDP_RSS_CONFIGURATION_REVISION_1 1

#define XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_RSS_CONFIGURATION, IndirectionTableSize)

//
// Initializes a RSS configuration object.
//
inline
VOID
XdpInitializeRssConfiguration(
    _Out_writes_bytes_(RssConfigurationSize) XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    RtlZeroMemory(RssConfiguration, RssConfigurationSize);
    RssConfiguration->Header.Revision = XDP_RSS_CONFIGURATION_REVISION_1;
    RssConfiguration->Header.Size = XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1;
}

//
// Set RSS settings on an interface. Configured settings will remain valid until
// the handle is closed. Upon handle closure, RSS settings will revert back to
// their original state.
//
typedef
HRESULT
XDP_RSS_SET_FN(
    _In_ HANDLE InterfaceHandle,
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    );

#define XDP_RSS_SET_FN_NAME "XdpRssSetExperimental"

//
// Query RSS settings on an interface. If the input RssConfigurationSize is too
// small, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) will be returned. Call
// with a NULL RssConfiguration to get the length.
//
typedef
HRESULT
XDP_RSS_GET_FN(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    );

#define XDP_RSS_GET_FN_NAME "XdpRssGetExperimental"

typedef enum _XDP_QUIC_OPERATION {
    XDP_QUIC_OPERATION_ADD,     // Add (or modify) a QUIC connection offload
    XDP_QUIC_OPERATION_REMOVE,  // Remove a QUIC connection offload
} XDP_QUIC_OPERATION;

typedef enum _XDP_QUIC_DIRECTION {
    XDP_QUIC_DIRECTION_TRANSMIT, // An offload for the transmit path
    XDP_QUIC_DIRECTION_RECEIVE,  // An offload for the receive path
} XDP_QUIC_DIRECTION;

typedef enum _XDP_QUIC_DECRYPT_FAILURE_ACTION {
    XDP_QUIC_DECRYPT_FAILURE_ACTION_DROP,     // Drop the packet on decryption failure
    XDP_QUIC_DECRYPT_FAILURE_ACTION_CONTINUE, // Continue and pass the packet up on decryption failure
} XDP_QUIC_DECRYPT_FAILURE_ACTION;

typedef enum _XDP_QUIC_CIPHER_TYPE {
    XDP_QUIC_CIPHER_TYPE_AEAD_AES_128_GCM,
    XDP_QUIC_CIPHER_TYPE_AEAD_AES_256_GCM,
    XDP_QUIC_CIPHER_TYPE_AEAD_CHACHA20_POLY1305,
    XDP_QUIC_CIPHER_TYPE_AEAD_AES_128_CCM,
} XDP_QUIC_CIPHER_TYPE;

typedef enum _XDP_QUIC_ADDRESS_FAMILY {
    XDP_QUIC_ADDRESS_FAMILY_INET4,
    XDP_QUIC_ADDRESS_FAMILY_INET6,
} XDP_QUIC_ADDRESS_FAMILY;

typedef struct _XDP_QUIC_CONNECTION {
    XDP_OBJECT_HEADER Header;
    UINT32 Operation            : 1;  // XDP_QUIC_OPERATION
    UINT32 Direction            : 1;  // XDP_QUIC_DIRECTION
    UINT32 DecryptFailureAction : 1;  // XDP_QUIC_DECRYPT_FAILURE_ACTION
    UINT32 KeyPhase             : 1;
    UINT32 RESERVED             : 12; // Must be set to 0. Don't read.
    UINT32 CipherType           : 16; // XDP_QUIC_CIPHER_TYPE
    XDP_QUIC_ADDRESS_FAMILY AddressFamily;
    UINT16 UdpPort;         // Destination port.
    UINT64 NextPacketNumber;
    UINT8 ConnectionIdLength;
    UINT8 Address[16];      // Destination IP address.
    UINT8 ConnectionId[20]; // QUIC v1 and v2 max CID size
    UINT8 PayloadKey[32];   // Length determined by CipherType
    UINT8 HeaderKey[32];    // Length determined by CipherType
    UINT8 PayloadIv[12];
    HRESULT Status;         // The result of trying to offload this connection.
} XDP_QUIC_CONNECTION;

#define XDP_QUIC_CONNECTION_REVISION_1 1

#define XDP_SIZEOF_QUIC_CONNECTION_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_QUIC_CONNECTION, Status)

//
// Initializes a QEO configuration object.
//
inline
VOID
XdpInitializeQuicConnection(
    _Out_writes_bytes_(XdpQuicConnectionSize) XDP_QUIC_CONNECTION *XdpQuicConnection,
    _In_ UINT32 XdpQuicConnectionSize
    )
{
    RtlZeroMemory(XdpQuicConnection, XdpQuicConnectionSize);
    XdpQuicConnection->Header.Revision = XDP_QUIC_CONNECTION_REVISION_1;
    XdpQuicConnection->Header.Size = XDP_SIZEOF_QUIC_CONNECTION_REVISION_1;
}

//
// Set QEO settings on an interface. Configured settings will remain valid until
// the handle is closed. Upon handle closure, QEO settings will revert back to
// their original state.
//
typedef HRESULT
XDP_QEO_SET_FN(
    _In_ HANDLE InterfaceHandle,
    _Inout_ XDP_QUIC_CONNECTION *QuicConnections,
    _In_ UINT32 QuicConnectionsSize
    );

#define XDP_QEO_SET_FN_NAME "XdpQeoSetExperimental"

#ifdef __cplusplus
} // extern "C"
#endif

#endif
