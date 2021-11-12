//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define OID_GEN_RECEIVE_SCALE_PARAMETERS        0x00010204  // query and set

typedef ULONG NDIS_OID, *PNDIS_OID;

typedef enum _NDIS_REQUEST_TYPE
{
    NdisRequestQueryInformation,
    NdisRequestSetInformation,
    NdisRequestQueryStatistics,
    NdisRequestMethod = 12,
} NDIS_REQUEST_TYPE, *PNDIS_REQUEST_TYPE;

typedef struct _NDIS_OBJECT_HEADER
{
    UCHAR
        Type;

    UCHAR
        Revision;

    USHORT
        Size;

} NDIS_OBJECT_HEADER, *PNDIS_OBJECT_HEADER;

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

#ifdef __cplusplus
} // extern "C"
#endif
