//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define OID_GEN_RECEIVE_BLOCK_SIZE              0x0001010B
#define OID_GEN_CURRENT_PACKET_FILTER           0x0001010E
#define OID_GEN_RECEIVE_SCALE_PARAMETERS        0x00010204  // query and set

typedef ULONG NDIS_OID, *PNDIS_OID;

typedef enum _NDIS_REQUEST_TYPE
{
    NdisRequestQueryInformation,
    NdisRequestSetInformation,
    NdisRequestQueryStatistics,
    NdisRequestMethod = 12,
} NDIS_REQUEST_TYPE, *PNDIS_REQUEST_TYPE;

#define NDIS_OBJECT_TYPE_RSS_PARAMETERS                     0x89    // used by miniport and protocol in NDIS_RECEIVE_SCALE_PARAMETERS

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

#ifdef __cplusplus
} // extern "C"
#endif
