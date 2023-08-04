//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This application provides a CLI to configure RSS via XDP. A configuration can
// be set and the settings will persist until explicitly cleared or until the
// application closes. No XSK sockets are used in this application.
//

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include <xdp/rtl.h>

typedef struct _INTERFACE_CONFIG_ENTRY {
    struct _INTERFACE_CONFIG_ENTRY *Next;
    UINT32 IfIndex;
    HANDLE InterfaceHandle;
} INTERFACE_CONFIG_ENTRY;

#define XDP_RSS_INDIRECTION_TABLE_SIZE 128

INTERFACE_CONFIG_ENTRY *InterfaceConfigList = NULL;

CONST XDP_API_TABLE *XdpApi;
XDP_RSS_SET_FN *XdpRssSet;
XDP_RSS_GET_FN *XdpRssGet;

CONST CHAR *UsageText =
"Usage:"
"\n    get <ifIndex>"
"\n        Gets the current RSS settings for a given interface."
"\n    set <ifIndex> <#,#,#,... >"
"\n        Sets the current RSS settings for a given interface. A comma separated"
"\n        list indicates the indirection table processor assignments."
"\n    clear <ifIndex>"
"\n        Clears the currently configured RSS settings for an interface, if any."
"\n";

VOID
Usage(
    _In_ const CHAR *Error
    )
{
    fprintf(stderr, "Error: %s\n%s", Error, UsageText);
}

BOOLEAN
FindInterfaceConfig(
    _In_ UINT32 IfIndex,
    _Out_ INTERFACE_CONFIG_ENTRY **Config,
    _Out_opt_ INTERFACE_CONFIG_ENTRY **PrevConfigEntry
    )
{
    BOOLEAN Success = FALSE;
    INTERFACE_CONFIG_ENTRY *Entry = InterfaceConfigList;
    INTERFACE_CONFIG_ENTRY *PrevEntry = NULL;

    while (Entry != NULL) {
        if (Entry->IfIndex == IfIndex) {
            Success = TRUE;
            break;
        }

        PrevEntry = Entry;
        Entry = Entry->Next;
    }

    *Config = Entry;
    if (PrevConfigEntry) {
        *PrevConfigEntry = PrevEntry;
    }

    return Success;
}

BOOLEAN
RemoveInterfaceConfig(
    _In_ UINT32 IfIndex
    )
{
    BOOLEAN Success = FALSE;
    INTERFACE_CONFIG_ENTRY *Entry;
    INTERFACE_CONFIG_ENTRY *PrevEntry;

    Success = FindInterfaceConfig(IfIndex, &Entry, &PrevEntry);
    if (!Success) {
        goto Exit;
    }

    if (PrevEntry != NULL) {
        PrevEntry->Next = Entry->Next;
    } else {
        InterfaceConfigList = Entry->Next;
    }

    if (Entry->InterfaceHandle != NULL) {
        CloseHandle(Entry->InterfaceHandle);
    }

    free(Entry);

Exit:

    return Success;
}

BOOLEAN
AddInterfaceConfig(
    _In_ UINT32 IfIndex
    )
{
    BOOLEAN Success = FALSE;
    HRESULT Result;
    INTERFACE_CONFIG_ENTRY *InterfaceConfig = NULL;

    InterfaceConfig = malloc(sizeof(*InterfaceConfig));
    if (InterfaceConfig == NULL) {
        printf("Error: Failed to allocate interface config\n");
        goto Exit;
    }

    RtlZeroMemory(InterfaceConfig, sizeof(*InterfaceConfig));

    Result = XdpApi->XdpInterfaceOpen(IfIndex, &InterfaceConfig->InterfaceHandle);
    if (FAILED(Result)) {
        printf("Error: Failed to open RSS handle on IfIndex=%u Result=%d\n", IfIndex, Result);
        goto Exit;
    }

    InterfaceConfig->IfIndex = IfIndex;
    InterfaceConfig->Next = InterfaceConfigList;
    InterfaceConfigList = InterfaceConfig;
    Success = TRUE;

Exit:

    if (!Success) {
        if (InterfaceConfig != NULL) {
            free(InterfaceConfig);
        }
    }

    return Success;
}

BOOLEAN
GetIfIndexParam(
    _In_ CHAR **StrTokContext,
    _Out_ UINT32 *IfIndex
    )
{
    CHAR *Str;

    *IfIndex = 0;

    Str = strtok_s(NULL, " \n", StrTokContext);
    if (Str == NULL) {
        printf("Error: could not parse IfIndex\n");
        return FALSE;
    }

    *IfIndex = atoi(Str);
    if (*IfIndex < 1) {
        printf("Error: invalid IfIndex\n");
        return FALSE;
    }

    return TRUE;
}

VOID
ProcessCommandGet(
    _In_ CHAR **StrTokContext
    )
{
    HRESULT Result;
    UINT32 IfIndex;
    HANDLE InterfaceHandle = NULL;
    XDP_RSS_CONFIGURATION *RssConfig = NULL;
    UINT32 RssConfigSize;
    UCHAR *HashSecretKey;
    PROCESSOR_NUMBER *IndirectionTable;

    if (!GetIfIndexParam(StrTokContext, &IfIndex)) {
        goto Exit;
    }

    Result = XdpApi->XdpInterfaceOpen(IfIndex, &InterfaceHandle);
    if (FAILED(Result)) {
        printf("Error: Failed to open RSS handle on IfIndex=%u Result=%d\n", IfIndex, Result);
        goto Exit;
    }

    RssConfigSize = 0;
    Result = XdpRssGet(InterfaceHandle, RssConfig, &RssConfigSize);
    if (SUCCEEDED(Result) || RssConfigSize < sizeof(RssConfig)) {
        printf(
            "Error: Failed to get RSS configuration size on IfIndex=%u Result=%d RssConfigSize=%d\n",
            IfIndex, Result, RssConfigSize);
        goto Exit;
    }

    RssConfig = malloc(RssConfigSize);
    if (RssConfig == NULL) {
        printf("Error: Failed to allocate RSS configuration for IfIndex=%u\n", IfIndex);
        goto Exit;
    }

    _Analysis_assume_(RssConfigSize >= sizeof(*RssConfig));
    Result = XdpRssGet(InterfaceHandle, RssConfig, &RssConfigSize);
    if (FAILED(Result)) {
        printf("Error: Failed to get RSS configuration on IfIndex=%u Result=%d\n", IfIndex, Result);
        goto Exit;
    }

    printf("RSS settings for IfIndex=%u\n", IfIndex);
    printf("RSS hash secret key:\n\t");
    HashSecretKey = (UCHAR *)RTL_PTR_ADD(RssConfig, RssConfig->HashSecretKeyOffset);
    for (int i = 0; i < RssConfig->HashSecretKeySize; i++) {
        printf("%02x", HashSecretKey[i]);
    }
    printf("\n");

    printf("RSS indirection table:\n\t");
    IndirectionTable = (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfig, RssConfig->IndirectionTableOffset);
    for (int i = 0; i < RssConfig->IndirectionTableSize / sizeof(PROCESSOR_NUMBER); i++) {
        printf("%u:%u ", IndirectionTable[i].Group, IndirectionTable[i].Number);
    }
    printf("\n");

Exit:

    if (RssConfig != NULL) {
        free(RssConfig);
    }

    if (InterfaceHandle != NULL) {
        CloseHandle(InterfaceHandle);
    }
}

VOID
ProcessCommandSet(
    _In_ CHAR **StrTokContext
    )
{
    HRESULT Result;
    UINT32 IfIndex;
    INTERFACE_CONFIG_ENTRY *InterfaceConfig;
    XDP_RSS_CONFIGURATION *RssConfig = NULL;
    UINT32 RssConfigSize;
    UINT32 ProcessorArraySize = 0;
    BOOLEAN AddedEntry = FALSE;
    BOOLEAN Success = FALSE;
    CHAR *Str;

    if (!GetIfIndexParam(StrTokContext, &IfIndex)) {
        goto Exit;
    }

    // Find or create.
    if (!FindInterfaceConfig(IfIndex, &InterfaceConfig, NULL)) {
        if (!AddInterfaceConfig(IfIndex)) {
            goto Exit;
        }
        AddedEntry = TRUE;
        if (!FindInterfaceConfig(IfIndex, &InterfaceConfig, NULL)) {
            printf("Error: Could not find interface config that was just added\n");
            goto Exit;
        }
    }

    RssConfigSize = sizeof(RssConfig) + XDP_RSS_INDIRECTION_TABLE_SIZE * sizeof(PROCESSOR_NUMBER);
    RssConfig = malloc(RssConfigSize);
    if (RssConfig == NULL) {
        printf("Error: Failed to allocate RSS configuration for IfIndex=%u\n", IfIndex);
        goto Exit;
    }

    XdpInitializeRssConfiguration(RssConfig, RssConfigSize);
    RssConfig->IndirectionTableOffset = sizeof(*RssConfig);

    PROCESSOR_NUMBER *IndirectionTable = RTL_PTR_ADD(RssConfig, RssConfig->IndirectionTableOffset);
    while ((Str = strtok_s(NULL, " ,\n", StrTokContext)) != NULL) {
        if (ProcessorArraySize == XDP_RSS_INDIRECTION_TABLE_SIZE) {
            printf("Error: Exceeded number of processor array entries\n");
            goto Exit;
        }

        IndirectionTable[ProcessorArraySize++].Number = (BYTE)atoi(Str);
    }

    RssConfig->IndirectionTableSize = (USHORT)(ProcessorArraySize * sizeof(PROCESSOR_NUMBER));
    RssConfig->Flags = XDP_RSS_FLAG_SET_INDIRECTION_TABLE;

    Result = XdpRssSet(InterfaceConfig->InterfaceHandle, RssConfig, RssConfigSize);
    if (FAILED(Result)) {
        printf("Error: Failed to set RSS configuration on IfIndex=%u Result=%d\n", IfIndex, Result);
        goto Exit;
    }

    Success = TRUE;

Exit:

    if (RssConfig != NULL) {
        free(RssConfig);
    }

    if (!Success) {
        if (AddedEntry) {
            RemoveInterfaceConfig(IfIndex);
        }
    }
}

VOID
ProcessCommandClear(
    _In_ CHAR **StrTokContext
    )
{
    UINT32 IfIndex;

    if (!GetIfIndexParam(StrTokContext, &IfIndex)) {
        goto Exit;
    }

    if (!RemoveInterfaceConfig(IfIndex)) {
        goto Exit;
    }

Exit:

    return;
}

INT
__cdecl
main()
{
    HRESULT Result = S_OK;
    CHAR Buffer[1024];
    CHAR *Str;
    CHAR *StrTokContext = NULL;

    Result = XdpOpenApi(XDP_API_VERSION_1, &XdpApi);
    if (FAILED(Result)) {
        printf("Error: Failed to load XDP API Result=%d\n", Result);
        goto Exit;
    }

    XdpRssSet = (XDP_RSS_SET_FN *)XdpApi->XdpGetRoutine(XDP_RSS_SET_FN_NAME);
    if (XdpRssSet == NULL) {
        printf("Error: Failed to load XdpRssSet Result=%d\n", Result);
        goto Exit;
    }

    XdpRssGet = (XDP_RSS_GET_FN *)XdpApi->XdpGetRoutine(XDP_RSS_GET_FN_NAME);
    if (XdpRssSet == NULL) {
        printf("Error: Failed to load XdpRssGet Result=%d\n", Result);
        goto Exit;
    }

    while (TRUE) {
        printf(">> ");
        if (fgets(Buffer, sizeof(Buffer), stdin) == NULL) {
            goto Exit;
        }

        Str = strtok_s(Buffer, " \n", &StrTokContext);

        if (!strcmp(Str, "get")) {
            ProcessCommandGet(&StrTokContext);
        } else if (!strcmp(Str, "set")) {
            ProcessCommandSet(&StrTokContext);
        } else if (!strcmp(Str, "clear")) {
            ProcessCommandClear(&StrTokContext);
        } else {
            Usage("invalid command");
        }
    }

Exit:

    if (XdpApi != NULL) {
        XdpRssGet = NULL;
        XdpRssSet = NULL;
        XdpCloseApi(XdpApi);
    }

    return FAILED(Result);
}
