//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <evntrace.h>
#include <tdh.h>
#include <wchar.h>

#include "xskbench.h"
#include "xskbenchdrvioctl.h"

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

typedef struct EventTracePropertyData {
    EVENT_TRACE_PROPERTIES Props;
    CHAR LoggerName[128];
    CHAR LogFileName[1024];
} EventTracePropertyData;

static char *SerializedArgs;
static SIZE_T SerializedArgsLength;
static HANDLE XskBenchThread;
static HANDLE XskBenchDrvHandle;
static TDH_CONTEXT TdhContext[1]; // May contain TDH_CONTEXT_WPP_TMFSEARCHPATH.
static BYTE TdhContextCount;      // 1 if a TMF search path is present.
static WCHAR FormattedStringBuffer[1024];
static TRACEHANDLE TraceHandle;
static EventTracePropertyData TraceData;
static const CHAR* TraceSessionName = "myrealtimetracesession";
static CHAR* NonConstTraceSessionName = "myrealtimetracesession";
static const GUID TraceSessionGuid = {
    0x9D2ED1FC, 0x45B1, 0x4FF8, 0x9C, 0x33, 0xC7, 0xA0, 0x5A, 0xB8, 0xE3, 0x11
};
static GUID XskBenchDrvProviderGuid = {
    0xF6871B1A, 0xC05D, 0x482B, 0x9C, 0xE9, 0x87, 0x16, 0xC9, 0xC2, 0x7E, 0x11
};

/*
This function will be used as the EventRecordCallback function in EVENT_TRACE_LOGFILE.
It expects that the EVENT_TRACE_LOGFILE's Context pointer is set to a DecoderContext.
*/
static void WINAPI EventRecordCallback(
    _In_ EVENT_RECORD* pEventRecord)
{
    // We expect that the EVENT_TRACE_LOGFILE.Context pointer was set with a
    // pointer to a DecoderContext. ProcessTrace will put the Context value
    // into EVENT_RECORD.UserContext.
    // DecoderContext* pContext = static_cast<DecoderContext*>(pEventRecord->UserContext);

    PROPERTY_DATA_DESCRIPTOR pdd = { (UINT_PTR)L"FormattedString" };

    ULONG status;
    ULONG cb = 0;
    status = TdhGetPropertySize(
        pEventRecord,
        TdhContextCount,
        TdhContextCount ? TdhContext : NULL,
        1,
        &pdd,
        &cb);
    if (status == ERROR_SUCCESS)
    {
        status = TdhGetProperty(
            pEventRecord,
            TdhContextCount,
            TdhContextCount ? TdhContext : NULL,
            1,
            &pdd,
            cb,
            (BYTE*)FormattedStringBuffer);
    }

    if (status != ERROR_SUCCESS)
    {
        // wprintf(L"[TdhGetProperty(%ls) error %u]", L"FormattedString", status);
    }
    else
    {
        // Print the FormattedString property data (nul-terminated
        // wchar_t string).
        wprintf(L"%ls\n", FormattedStringBuffer);
    }
}

void StopTracing()
{
    printf("stop tracing\n");
    if (TraceHandle != 0) {
        ULONG Err = StopTraceA(TraceHandle, TraceSessionName, &TraceData.Props);
        TraceHandle = 0;
        if (Err != ERROR_SUCCESS) {
            printf("WARNING: Could not stop trace session (%d)\n", Err);
        }
    }
}

ULONG StartTracing()
{
    ULONG Err;

    TraceHandle = 0;

    ZeroMemory(&TraceData, sizeof(TraceData));
    TraceData.Props.Wnode.BufferSize = sizeof(TraceData);
    TraceData.Props.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    TraceData.Props.Wnode.Guid = TraceSessionGuid;
    TraceData.Props.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    TraceData.Props.MaximumFileSize = 256;  // 256 MB
    TraceData.Props.LogFileNameOffset = offsetof(EventTracePropertyData, LogFileName);
    TraceData.Props.LoggerNameOffset = offsetof(EventTracePropertyData, LoggerName);

    //
    // Stop the tracing session if it exists already.
    //
    StopTraceA(TraceHandle, TraceSessionName, &TraceData.Props);

    Err = strcpy_s(TraceData.LoggerName, sizeof(TraceData.LoggerName), TraceSessionName);
    if (Err != 0) {
        printf("ERROR: Could not create logger name\n");
        Err = ERROR_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // Start the trace session.
    //
    Err = StartTraceA(&TraceHandle, TraceSessionName, &TraceData.Props);
    if (Err != ERROR_SUCCESS) {
        printf("ERROR: Could not start trace (%d)\n", Err);
        goto Exit;
    }

    //
    // Enable providers.
    //
    Err =
        EnableTrace(TRUE, 0xFFFFFFFF, TRACE_LEVEL_VERBOSE, (LPCGUID)&XskBenchDrvProviderGuid, TraceHandle);
    if (Err != ERROR_SUCCESS) {
        printf("ERROR: Could not enable trace provider (%d)\n", Err);
        goto Exit;
    }

Exit:
    if (Err != ERROR_SUCCESS && TraceHandle != 0) {
        StopTracing();
    }
    return Err;
}

HRESULT
XdpBenchDrvOpen(
    _In_ UINT32 Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ UINT32 EaLength,
    _Out_ HANDLE *Handle
    )
{
    UNICODE_STRING DeviceName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    //
    // Open a handle to the XSKBENCHDRV device.
    //
    RtlInitUnicodeString(&DeviceName, XSKBENCHDRV_DEVICE_NAME);
    InitializeObjectAttributes(
        &ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status =
        NtCreateFile(
            Handle,
            GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
            &ObjectAttributes,
            &IoStatusBlock,
            NULL,
            0L,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            Disposition,
            0,
            EaBuffer,
            EaLength);

    return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
}

HRESULT
XdpBenchDrvIoctl(
    _In_ HANDLE XdpHandle,
    _In_ UINT32 Operation,
    _In_opt_ VOID *InBuffer,
    _In_ UINT32 InBufferSize,
    _Out_opt_ VOID *OutBuffer,
    _In_ UINT32 OutputBufferSize,
    _Out_opt_ UINT32 *BytesReturned,
    _In_opt_ OVERLAPPED *Overlapped
    )
{
    NTSTATUS Status;
    IO_STATUS_BLOCK LocalIoStatusBlock = {0};
    IO_STATUS_BLOCK *IoStatusBlock;
    HANDLE LocalEvent = NULL;
    HANDLE *Event;

    if (BytesReturned != NULL) {
        *BytesReturned = 0;
    }

    if (Overlapped == NULL) {
        IoStatusBlock = &LocalIoStatusBlock;
        Event = &LocalEvent;
        LocalEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (LocalEvent == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
    } else {
        IoStatusBlock = (IO_STATUS_BLOCK *)&Overlapped->Internal;
        Event = &Overlapped->hEvent;
    }

    IoStatusBlock->Status = STATUS_PENDING;

    Status =
        NtDeviceIoControlFile(
            XdpHandle, *Event, NULL, NULL, IoStatusBlock, Operation, InBuffer,
            InBufferSize, OutBuffer, OutputBufferSize);

    if (Event == &LocalEvent && Status == STATUS_PENDING) {
        DWORD WaitResult = WaitForSingleObject(*Event, INFINITE);
        if (WaitResult != WAIT_OBJECT_0) {
            if (WaitResult != WAIT_FAILED) {
                Status = STATUS_UNSUCCESSFUL;
            }
            goto Exit;
        }

        Status = IoStatusBlock->Status;
    }

    if (BytesReturned != NULL) {
        *BytesReturned = (UINT32)IoStatusBlock->Information;
    }

Exit:

    if (LocalEvent != NULL) {
        CloseHandle(LocalEvent);
    }

    return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
}

DWORD
WINAPI
XskBenchFunction(
    _In_ LPVOID lpParameter
    )
{
    HRESULT Result;

    UNREFERENCED_PARAMETER(lpParameter);

    printf("opening driver\n");
    Result = XdpBenchDrvOpen(FILE_CREATE, NULL, 0, &XskBenchDrvHandle);
    if (FAILED(Result)) {
        printf("XdpBenchDrvOpen failed (%d)\n", Result);
        goto Exit;
    }

    printf("issuing IOCTL_START_SESSION\n");
    Result = XdpBenchDrvIoctl(XskBenchDrvHandle, IOCTL_START_SESSION, SerializedArgs, (UINT32)SerializedArgsLength, NULL, 0, NULL, NULL);
    if (FAILED(Result)) {
        printf("XdpBenchDrvIoctl(IOCTL_START_SESSION) failed (%d)\n", Result);
        goto Exit;
    }

    printf("session exited successsfully\n");

Exit:

    if (XskBenchDrvHandle != NULL) {
        printf("closing driver\n");
        CloseHandle(XskBenchDrvHandle);
        XskBenchDrvHandle = NULL;
    }

    StopTracing();

    printf("XskBenchFunction exiting\n");

    return Result;
}

BOOLEAN
SerializeArgs(
    INT argc,
    CHAR **argv
    )
{
    // xskbench arg parsing expects program name as the first arg and ignores it.
    // For convenience, pass the tmfpath arg as the first arg and xskbench will ignore
    const INT argOffset = 1;

    // find length of serialized string
    SerializedArgsLength = 0;
    for (int i = argOffset; i < argc; i++) {
        SerializedArgsLength += strlen(argv[i]) + 1; // +1 for terminating null
    }

    // allocate serialized string
    SerializedArgs = malloc(SerializedArgsLength);
    if (SerializedArgs == NULL) {
        printf("maloc failed\n");
        return FALSE;
    }

    // populate serialized string
    for (int i = argOffset, j = 0; i < argc; i++) {
        if (strcpy_s(&SerializedArgs[j], SerializedArgsLength - j, argv[i]) != 0) {
            printf("strcpy_s failed\n");
            return FALSE;
        }
        j += (int)strlen(argv[i]) + 1; // +1 for terminating null
    }

    return TRUE;
}

BOOL
WINAPI
ConsoleCtrlHandler(
    DWORD CtrlType
    )
{
    HRESULT Result;

    UNREFERENCED_PARAMETER(CtrlType);

    printf("CTRL-C\n");

    if (XskBenchDrvHandle != NULL) {
        printf("issuing IOCTL_INTERRUPT_SESSION\n");
        Result = XdpBenchDrvIoctl(XskBenchDrvHandle, IOCTL_INTERRUPT_SESSION, NULL, 0, NULL, 0, NULL, NULL);
        if (FAILED(Result)) {
            printf("XdpBenchDrvIoctl(IOCTL_INTERRUPT_SESSION) failed (%d)\n", Result);
            goto Exit;
        }
    }

Exit:

    StopTracing();

    printf("ConsoleCtrlHandler exiting\n");
    return TRUE;
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    // Get the TMF file path
    if (argc < 2) {
        printf("First argument must be a tmfpath\nxskbench_km <tmfpath> <rest of the xskbench args>\n");
        exit(0);
    }
    char* tmfpath = argv[1];

    size_t len = strlen(tmfpath);
    WCHAR unitmfpath[MAX_PATH];
    int result = MultiByteToWideChar(CP_OEMCP, 0, tmfpath, -1, unitmfpath, (int) len + 1);
    if (result != strlen(tmfpath) + 1) {
        printf("could not convert to tmfpath %llu != %d\n", strlen(tmfpath), result);
        exit(0);
    }

    // Initialize TDH stuff
    TDH_CONTEXT* p = TdhContext;
    p->ParameterValue = (UINT_PTR)unitmfpath;
    p->ParameterType = TDH_CONTEXT_WPP_TMFSEARCHPATH;
    p->ParameterSize = 0;
    p += 1;
    TdhContextCount = (BYTE)(p - TdhContext);

    // start trace session
    if (StartTracing() != ERROR_SUCCESS) {
        return 0;
    }

    // start real-time trace processing
    EVENT_TRACE_LOGFILE Logfile;
    ZeroMemory(&Logfile, sizeof(EVENT_TRACE_LOGFILE));
    Logfile.LoggerName = NonConstTraceSessionName;
    Logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    Logfile.EventRecordCallback = EventRecordCallback;

    TraceHandle = OpenTrace(&Logfile);
    if (TraceHandle == INVALID_PROCESSTRACE_HANDLE) {
        printf("Failed to open trace\n");
        return 0;
    }

    if (!SerializeArgs(argc, argv)) {
        goto Exit;
    }

    XskBenchThread = CreateThread(NULL, 0, XskBenchFunction, NULL, 0, NULL);
    if (XskBenchThread == NULL) {
        printf("Failed to create thread\n");
        goto Exit;
    }

    ULONG Status = ProcessTrace(&TraceHandle, 1, NULL, NULL);
    if (Status != ERROR_SUCCESS) {
        printf("Failed to process trace\n");
    }


Exit:
    printf("exit main\n");
    // stop trace session

    return 0;
}
