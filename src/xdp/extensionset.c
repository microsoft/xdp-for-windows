//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _XDP_EXTENSION_ENTRY {
    BOOLEAN Enabled;
    BOOLEAN InterfaceRegistered;
    BOOLEAN InternalExtension;
    BOOLEAN Assigned;
    UINT8 Size;
    UINT8 Alignment;
    UINT16 AssignedOffset;
    XDP_EXTENSION_INFO Info;
    XDP_EXTENSION Extension;
} XDP_EXTENSION_ENTRY;

typedef struct _XDP_EXTENSION_SET {
    XDP_EXTENSION_TYPE Type;
    UINT16 Count;
    XDP_EXTENSION_ENTRY Entries[0];
} XDP_EXTENSION_SET;

static
XDP_EXTENSION_ENTRY *
XdpExtensionSetFindEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName
    )
{
    XDP_EXTENSION_ENTRY *Entry = NULL;

    for (UINT16 Index = 0; Index < ExtensionSet->Count; Index++) {
        XDP_EXTENSION_ENTRY *Candidate = &ExtensionSet->Entries[Index];

        if (wcscmp(Candidate->Info.ExtensionName, ExtensionName) == 0) {
            Entry = Candidate;
            break;
        }
    }

    return Entry;
}

static
VOID
XdpExtensionSetValidate(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_ CONST XDP_EXTENSION_INFO *Info
    )
{

    FRE_ASSERT(Info->ExtensionType == ExtensionSet->Type);
    FRE_ASSERT(Info->ExtensionName != NULL);
    FRE_ASSERT(Info->ExtensionVersion > 0);
}

static
VOID
XdpExtensionSetValidateAlignment(
    _In_ UINT8 Alignment
    )
{
    FRE_ASSERT(RTL_IS_POWER_OF_TWO(Alignment));
    FRE_ASSERT(Alignment <= SYSTEM_CACHE_ALIGNMENT_SIZE);
}

static
int
__cdecl
XdpExtensionSetCompare(
    const void *Key1,
    const void *Key2
    )
{
    CONST XDP_EXTENSION_ENTRY *Entry1 = Key1;
    CONST XDP_EXTENSION_ENTRY *Entry2 = Key2;

    return (int)Entry2->Alignment - (int)Entry1->Alignment;
}

NTSTATUS
XdpExtensionSetAssignLayout(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_ UINT32 BaseOffset,
    _In_ UINT8 BaseAlignment,
    _Out_ UINT32 *Size,
    _Out_ UINT8 *Alignment
    )
{
    UINT32 Offset = BaseOffset;
    UINT8 MaxAlignment = BaseAlignment;
    UINT8 CurrentAlignment;

    if (Offset > 0) {
        CurrentAlignment = 1 << RtlFindLeastSignificantBit(Offset);
    } else {
        //
        // No alignment contraints.
        //
        CurrentAlignment = 0x80;
    }

    //
    // TODO: This layout algorithm is nowhere near optimal.
    //
    // Make two passes through the extensions: first constrained by the initial
    // offset and then unconstrained for the remainder. Assigning extensions by
    // decreasing alignment reduces padding.
    //

    qsort(
        ExtensionSet->Entries, ExtensionSet->Count, sizeof(ExtensionSet->Entries[0]),
        XdpExtensionSetCompare);

    for (UINT8 Iteration = 0; Iteration <= 1; Iteration++) {
        for (UINT16 Index = 0; Index < ExtensionSet->Count; Index++) {
            XDP_EXTENSION_ENTRY *Entry = &ExtensionSet->Entries[Index];

            FRE_ASSERT(!Entry->Enabled || Entry->InternalExtension || Entry->InterfaceRegistered);

            if (!Entry->Enabled || Entry->Assigned) {
                continue;
            }

            if (Iteration == 1) {
                //
                // Insert padding to force alignment.
                //
                Offset = ALIGN_UP_BY(Offset, Entry->Alignment);
            } else if (CurrentAlignment < Entry->Alignment) {
                continue;
            }

            if (Offset > MAXUINT16) {
                return STATUS_INTEGER_OVERFLOW;
            }

            if (MaxAlignment < Entry->Alignment) {
                MaxAlignment = Entry->Alignment;
            }

            Entry->AssignedOffset = (UINT16)Offset;
            Entry->Assigned = TRUE;

            //
            // TODO: optimize layout of packed structures.
            //
            Offset += ALIGN_UP_BY(Entry->Size, Entry->Alignment);
            CurrentAlignment = Entry->Alignment;
        }
    }

    *Size = ALIGN_UP_BY(Offset, MaxAlignment);
    *Alignment = MaxAlignment;

    return STATUS_SUCCESS;
}

VOID
XdpExtensionSetRegisterEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_ XDP_EXTENSION_INFO *Info
    )
{
    XDP_EXTENSION_ENTRY *Entry;

    XdpExtensionSetValidate(ExtensionSet, Info);

    Entry = XdpExtensionSetFindEntry(ExtensionSet, Info->ExtensionName);
    FRE_ASSERT(Entry != NULL);

    //
    // Multiple extension versions are not implemented.
    //
    FRE_ASSERT(Entry->Info.ExtensionVersion == Info->ExtensionVersion);

    Entry->InterfaceRegistered = TRUE;
}

VOID
XdpExtensionSetEnableEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName
    )
{
    XDP_EXTENSION_ENTRY *Entry;

    Entry = XdpExtensionSetFindEntry(ExtensionSet, ExtensionName);
    FRE_ASSERT(Entry != NULL);

    Entry->Enabled = TRUE;
}

VOID
XdpExtensionSetSetInternalEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName
    )
{
    XDP_EXTENSION_ENTRY *Entry;

    //
    // This routine marks an extension entry as internal to XDP: it can be
    // enabled for use within XDP without the interface registering its
    // extension info.
    //
    // TODO: This should also prohibit interfaces retrieving the extension.
    //

    Entry = XdpExtensionSetFindEntry(ExtensionSet, ExtensionName);
    FRE_ASSERT(Entry != NULL);

    Entry->InternalExtension = TRUE;
}

VOID
XdpExtensionSetResizeEntry(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName,
    _In_ UINT8 Size,
    _In_ UINT8 Alignment
    )
{
    XDP_EXTENSION_ENTRY *Entry;

    XdpExtensionSetValidateAlignment(Alignment);

    Entry = XdpExtensionSetFindEntry(ExtensionSet, ExtensionName);
    FRE_ASSERT(Entry != NULL);

    Entry->Size = Size;
    Entry->Alignment = Alignment;
}

VOID
XdpExtensionSetGetExtension(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    )
{
    XDP_EXTENSION_ENTRY *Entry;

    FRE_ASSERT(ExtensionInfo->ExtensionType == ExtensionSet->Type);

    Entry = XdpExtensionSetFindEntry(ExtensionSet, ExtensionInfo->ExtensionName);
    FRE_ASSERT(Entry != NULL);
    FRE_ASSERT(Entry->Enabled);
    FRE_ASSERT(Entry->Assigned);
    FRE_ASSERT(Entry->Info.ExtensionVersion >= ExtensionInfo->ExtensionVersion);

    Extension->Reserved = Entry->AssignedOffset;
}

BOOLEAN
XdpExtensionSetIsExtensionEnabled(
    _In_ XDP_EXTENSION_SET *ExtensionSet,
    _In_z_ CONST WCHAR *ExtensionName
    )
{
    XDP_EXTENSION_ENTRY *Entry;

    Entry = XdpExtensionSetFindEntry(ExtensionSet, ExtensionName);
    FRE_ASSERT(Entry != NULL);

    return Entry->Enabled;
}

NTSTATUS
XdpExtensionSetCreate(
    _In_ XDP_EXTENSION_TYPE Type,
    _In_opt_count_(ReservedExtensionCount) CONST XDP_EXTENSION_REGISTRATION *ReservedExtensions,
    _In_ UINT16 ReservedExtensionCount,
    _Out_ XDP_EXTENSION_SET **ExtensionSet
    )
{
    XDP_EXTENSION_SET *Set;
    NTSTATUS Status;

    Set =
        ExAllocatePoolZero(
            PagedPool, sizeof(*Set) + sizeof(Set->Entries[0]) * ReservedExtensionCount,
            XDP_POOLTAG_EXTENSION);
    if (Set == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Set->Type = Type;
    Set->Count = ReservedExtensionCount;

    for (UINT16 Index = 0; Index < Set->Count; Index++) {
        XDP_EXTENSION_ENTRY *Entry = &Set->Entries[Index];
        ASSERT(ReservedExtensions);
        CONST XDP_EXTENSION_REGISTRATION *Reg = &ReservedExtensions[Index];

        XdpExtensionSetValidate(Set, &Reg->Info);
        XdpExtensionSetValidateAlignment(Reg->Alignment);

        Entry->Enabled = FALSE;
        Entry->Info = Reg->Info;
        Entry->Size = Reg->Size;
        Entry->Alignment = Reg->Alignment;
    }

    *ExtensionSet = Set;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

VOID
XdpExtensionSetCleanup(
    _In_ XDP_EXTENSION_SET *ExtensionSet
    )
{
    ExFreePoolWithTag(ExtensionSet, XDP_POOLTAG_EXTENSION);
}
