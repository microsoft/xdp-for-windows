//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This sample demonstrates how to create an AF_XDP socket and attach an eBPF
// XDP program in a privileged parent process, then duplicate the socket handle
// into a child process running with a restricted token. The child process uses
// the duplicated handle to perform RX-to-TX forwarding without needing elevated
// privileges itself.
//
// Approach:
//   1. Parent loads eBPF program, creates AF_XDP socket, populates XSKMAP.
//   2. Parent attaches eBPF program to the interface.
//   3. Parent creates a restricted token (Administrators SID disabled).
//   4. Parent spawns a child process (suspended) with the restricted token.
//   5. Parent duplicates the socket handle into the child.
//   6. Parent writes handle value to a named pipe the child reads on startup.
//   7. Parent resumes the child, which configures rings and forwards packets.
//

#include <xdpapi.h>
#include <afxdp_helper.h>
#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#pragma warning(pop)
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

CONST CHAR *UsageText =
"Usage:\n"
"  xskrestricted.exe <IfIndex> [-BpfProgram <path.sys>] [-ProgramName <name>]\n"
"                              [-TimeoutSeconds <Seconds>]\n"
"\n"
"Loads an eBPF XDP program, creates an AF_XDP socket, then spawns a child\n"
"process with a restricted token and duplicates the socket handle into it.\n"
"The child forwards UDP port 1234 traffic back to the sender.\n"
"\n"
"Options:\n"
"  -BpfProgram <path.sys>      Path to compiled eBPF program.\n"
"                              Default: xskrestricted_redirect.sys\n"
"  -ProgramName <name>         eBPF program entry point name.\n"
"                              Default: xskrestricted_redirect\n"
"  -TimeoutSeconds <Seconds>   Exit after the specified number of seconds.\n"
"                              If 0 or omitted, run indefinitely.\n";

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

#define LOGINFO(...) \
    fprintf(stdout, "INFO: "); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n")


//
// A simple structure passed from the parent to the child via a named pipe.
//
typedef struct _XSKRESTRICTED_CHILD_PARAMS {
    UINT64 SocketHandle;
    UINT32 IfIndex;
    UINT32 TimeoutSeconds;
} XSKRESTRICTED_CHILD_PARAMS;

//
// Child process: read duplicated handle values from the named pipe, configure
// the socket, and run the RX-to-TX forwarding loop.
//
static
INT
RunChild(
    _In_ const WCHAR *PipeName
    )
{
    HRESULT Result;
    HANDLE PipeHandle = INVALID_HANDLE_VALUE;
    XSKRESTRICTED_CHILD_PARAMS Params;
    DWORD BytesRead;
    HANDLE Socket;
    UCHAR Frame[1514];
    XSK_UMEM_REG UmemReg = {0};
    const UINT32 RingSize = 1;
    XSK_RING_INFO_SET RingInfo;
    UINT32 OptionLength;
    XSK_RING RxRing;
    XSK_RING RxFillRing;
    XSK_RING TxRing;
    XSK_RING TxCompRing;
    UINT32 RingIndex;
    HANDLE CreatedXsk;

    PipeHandle =
        CreateFileW(
            PipeName,
            GENERIC_READ,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);
    if (PipeHandle == INVALID_HANDLE_VALUE) {
        LOGERR("[Child] Failed to open pipe: %u", GetLastError());
        return 1;
    }

    if (!ReadFile(PipeHandle, &Params, sizeof(Params), &BytesRead, NULL) ||
        BytesRead != sizeof(Params)) {
        LOGERR("[Child] Failed to read params from pipe: %u", GetLastError());
        CloseHandle(PipeHandle);
        return 1;
    }
    CloseHandle(PipeHandle);

    Socket = (HANDLE)(ULONG_PTR)Params.SocketHandle;

    LOGINFO(
        "[Child] Running with duplicated socket handle=%p IfIndex=%u",
        Socket, Params.IfIndex);

    //
    // Verify this restricted process can't create sockets.
    //
    if (XskCreate(&CreatedXsk) != HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
        LOGERR("[Child] XskCreate was not denied access");
        return 1;
    }

    //
    // Register our frame buffer with the AF_XDP socket.
    //
    UmemReg.TotalSize = sizeof(Frame);
    UmemReg.ChunkSize = sizeof(Frame);
    UmemReg.Address = Frame;

    Result = XskSetSockopt(Socket, XSK_SOCKOPT_UMEM_REG, &UmemReg, sizeof(UmemReg));
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XSK_UMEM_REG failed: %x", Result);
        return 1;
    }

    Result = XskSetSockopt(Socket, XSK_SOCKOPT_RX_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XSK_SOCKOPT_RX_RING_SIZE failed: %x", Result);
        return 1;
    }

    Result = XskSetSockopt(Socket, XSK_SOCKOPT_RX_FILL_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XSK_SOCKOPT_RX_FILL_RING_SIZE failed: %x", Result);
        return 1;
    }

    Result = XskSetSockopt(Socket, XSK_SOCKOPT_TX_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XSK_SOCKOPT_TX_RING_SIZE failed: %x", Result);
        return 1;
    }

    Result = XskSetSockopt(Socket, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XSK_SOCKOPT_TX_COMPLETION_RING_SIZE failed: %x", Result);
        return 1;
    }

    Result = XskActivate(Socket, XSK_ACTIVATE_FLAG_NONE);
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XskActivate failed: %x", Result);
        return 1;
    }

    OptionLength = sizeof(RingInfo);
    Result = XskGetSockopt(Socket, XSK_SOCKOPT_RING_INFO, &RingInfo, &OptionLength);
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XSK_SOCKOPT_RING_INFO failed: %x", Result);
        return 1;
    }

    XskRingInitialize(&RxRing, &RingInfo.Rx);
    XskRingInitialize(&RxFillRing, &RingInfo.Fill);
    XskRingInitialize(&TxRing, &RingInfo.Tx);
    XskRingInitialize(&TxCompRing, &RingInfo.Completion);

    //
    // Place an empty frame descriptor into the RX fill ring.
    //
    XskRingProducerReserve(&RxFillRing, 1, &RingIndex);
    *(UINT64 *)XskRingGetElement(&RxFillRing, RingIndex) = 0;
    XskRingProducerSubmit(&RxFillRing, 1);

    LOGINFO("[Child] Forwarding packets (Ctrl+C to stop)...");

    {
    ULONGLONG Deadline = 0;
    if (Params.TimeoutSeconds > 0) {
        Deadline = GetTickCount64() + ((ULONGLONG)Params.TimeoutSeconds * 1000);
        LOGINFO("[Child] Will exit after %u seconds", Params.TimeoutSeconds);
    }

    for (;;) {
        if (Deadline != 0 && GetTickCount64() >= Deadline) {
            LOGINFO("[Child] Timeout reached, exiting");
            break;
        }
        if (XskRingConsumerReserve(&RxRing, 1, &RingIndex) == 1) {
            XSK_BUFFER_DESCRIPTOR *RxBuffer;
            XSK_BUFFER_DESCRIPTOR *TxBuffer;
            XSK_NOTIFY_RESULT_FLAGS NotifyResult;

            RxBuffer = XskRingGetElement(&RxRing, RingIndex);

            XskRingProducerReserve(&TxRing, 1, &RingIndex);
            TxBuffer = XskRingGetElement(&TxRing, RingIndex);

            {
                UCHAR *FrameData =
                    &Frame[RxBuffer->Address.BaseAddress + RxBuffer->Address.Offset];
                UINT32 Length = RxBuffer->Length;
                UCHAR MacAddress[6];

                if (Length >= sizeof(MacAddress) * 2) {
                    RtlCopyMemory(MacAddress, FrameData, sizeof(MacAddress));
                    RtlCopyMemory(FrameData, FrameData + sizeof(MacAddress), sizeof(MacAddress));
                    RtlCopyMemory(FrameData + sizeof(MacAddress), MacAddress, sizeof(MacAddress));
                }
            }

            *TxBuffer = *RxBuffer;

            XskRingConsumerRelease(&RxRing, 1);
            XskRingProducerSubmit(&TxRing, 1);

            Result = XskNotifySocket(Socket, XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
            if (XDP_FAILED(Result)) {
                LOGERR("[Child] XskNotifySocket failed: %x", Result);
                return 1;
            }
        }

        if (XskRingConsumerReserve(&TxCompRing, 1, &RingIndex) == 1) {
            UINT64 *Tx = XskRingGetElement(&TxCompRing, RingIndex);
            UINT64 *Rx;

            XskRingProducerReserve(&RxFillRing, 1, &RingIndex);
            Rx = XskRingGetElement(&RxFillRing, RingIndex);
            *Rx = *Tx;

            XskRingConsumerRelease(&TxCompRing, 1);
            XskRingProducerSubmit(&RxFillRing, 1);
        }
    }
    }

    LOGINFO("[Child] Exiting successfully");
    CloseHandle(Socket);

    return 0;
}

//
// Parent process: load eBPF program, create AF_XDP socket, populate XSKMAP,
// create a restricted child process, and duplicate the socket handle.
//
static
INT
RunParent(
    _In_ UINT32 IfIndex,
    _In_ const CHAR *BpfProgramPath,
    _In_ const CHAR *ProgramName,
    _In_ UINT32 TimeoutSeconds
    )
{
    HRESULT HResult;
    int Result;
    struct bpf_object *BpfObject = NULL;
    struct bpf_program *BpfProgram = NULL;
    int ProgramFd;
    fd_t XskMapFd;
    HANDLE Socket = NULL;
    HANDLE RestrictedToken = NULL;
    HANDLE CurrentToken = NULL;
    SID_AND_ATTRIBUTES SidsToDisable[1];
    DWORD SidSize;
    PSID AdminSid = NULL;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    WCHAR CommandLine[512];
    HANDLE ChildSocket = NULL;
    INT ExitCode = 1;
    WCHAR ModulePathW[MAX_PATH];
    HANDLE PipeHandle = INVALID_HANDLE_VALUE;
    WCHAR PipeName[128];
    XSKRESTRICTED_CHILD_PARAMS ChildParams;
    DWORD BytesWritten;
    SECURITY_ATTRIBUTES PipeSa;
    BOOLEAN ProcessCreated = FALSE;
    UINT32 QueueId = 0;

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    LOGINFO("[Parent] Loading eBPF program and creating socket for IfIndex=%u", IfIndex);

    //
    // Create a named pipe for communicating with the child.
    //
    _snwprintf_s(
        PipeName, ARRAYSIZE(PipeName), _TRUNCATE,
        L"\\\\.\\pipe\\xskrestricted_%u", GetCurrentProcessId());

    ZeroMemory(&PipeSa, sizeof(PipeSa));
    PipeSa.nLength = sizeof(PipeSa);
    PipeSa.bInheritHandle = FALSE;

    PipeHandle =
        CreateNamedPipeW(
            PipeName,
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_BYTE | PIPE_WAIT,
            1,
            sizeof(XSKRESTRICTED_CHILD_PARAMS),
            0,
            0,
            &PipeSa);
    if (PipeHandle == INVALID_HANDLE_VALUE) {
        LOGERR("[Parent] CreateNamedPipeW failed: %u", GetLastError());
        goto Exit;
    }

    HResult = XskCreate(&Socket);
    if (XDP_FAILED(HResult)) {
        LOGERR("[Parent] XskCreate failed: %x", HResult);
        goto Exit;
    }

    HResult = XskBind(Socket, IfIndex, QueueId, XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX);
    if (XDP_FAILED(HResult)) {
        LOGERR("[Parent] XskBind failed: %x", HResult);
        goto Exit;
    }

    LOGINFO("[Parent] Socket created and bound (handle=%p)", Socket);

    BpfObject = bpf_object__open(BpfProgramPath);
    if (BpfObject == NULL) {
        LOGERR("[Parent] bpf_object__open(%s) failed", BpfProgramPath);
        goto Exit;
    }

    Result = bpf_object__load(BpfObject);
    if (Result != 0) {
        LOGERR("[Parent] bpf_object__load failed: %d", Result);
        goto Exit;
    }

    BpfProgram = bpf_object__find_program_by_name(BpfObject, ProgramName);
    if (BpfProgram == NULL) {
        LOGERR("[Parent] Program '%s' not found", ProgramName);
        goto Exit;
    }

    ProgramFd = bpf_program__fd(BpfProgram);
    if (ProgramFd < 0) {
        LOGERR("[Parent] bpf_program__fd failed");
        goto Exit;
    }

    XskMapFd = bpf_object__find_map_fd_by_name(BpfObject, "xsk_map");
    if (XskMapFd < 0) {
        LOGERR("[Parent] xsk_map not found");
        goto Exit;
    }

    UINT64 MapKey = QueueId;
    Result = bpf_map_update_elem(XskMapFd, &MapKey, &Socket, 0);
    if (Result != 0) {
        LOGERR("[Parent] bpf_map_update_elem(xsk_map) failed: %d", Result);
        goto Exit;
    }

    Result = bpf_xdp_attach(IfIndex, ProgramFd, 0, NULL);
    if (Result != 0) {
        LOGERR("[Parent] bpf_xdp_attach failed: %d", Result);
        goto Exit;
    }

    LOGINFO("[Parent] eBPF program attached");

    //
    // Create a restricted token by disabling the Administrators group SID.
    //
    SidSize = SECURITY_MAX_SID_SIZE;
    AdminSid = LocalAlloc(LMEM_FIXED, SidSize);
    if (AdminSid == NULL) {
        LOGERR("[Parent] LocalAlloc for SID failed");
        goto Exit;
    }

    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, AdminSid, &SidSize)) {
        LOGERR("[Parent] CreateWellKnownSid failed: %u", GetLastError());
        goto Exit;
    }

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &CurrentToken)) {
        LOGERR("[Parent] OpenProcessToken failed: %u", GetLastError());
        goto Exit;
    }

    SidsToDisable[0].Sid = AdminSid;
    SidsToDisable[0].Attributes = 0;

    if (!CreateRestrictedToken(
            CurrentToken, 0, 1, SidsToDisable, 0, NULL, 0, NULL, &RestrictedToken)) {
        LOGERR("[Parent] CreateRestrictedToken failed: %u", GetLastError());
        goto Exit;
    }

    LOGINFO("[Parent] Restricted token created (Administrators SID disabled)");

    //
    // Create the child process in a suspended state.
    //
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    if (GetModuleFileNameW(NULL, ModulePathW, ARRAYSIZE(ModulePathW)) == 0) {
        LOGERR("[Parent] GetModuleFileNameW failed: %u", GetLastError());
        goto Exit;
    }

    _snwprintf_s(
        CommandLine, ARRAYSIZE(CommandLine), _TRUNCATE,
        L"\"%s\" -child \"%s\"",
        ModulePathW, PipeName);

    if (!CreateProcessAsUserW(
            RestrictedToken, NULL, CommandLine, NULL, NULL, FALSE,
            CREATE_SUSPENDED, NULL, NULL, &StartupInfo, &ProcessInfo)) {
        LOGERR("[Parent] CreateProcessAsUserW failed: %u", GetLastError());
        goto Exit;
    }

    ProcessCreated = TRUE;

    LOGINFO("[Parent] Child process created (PID=%u), suspended", ProcessInfo.dwProcessId);

    if (!DuplicateHandle(
            GetCurrentProcess(), Socket, ProcessInfo.hProcess, &ChildSocket,
            0, FALSE, DUPLICATE_SAME_ACCESS)) {
        LOGERR("[Parent] DuplicateHandle(Socket) failed: %u", GetLastError());
        goto Exit;
    }

    LOGINFO("[Parent] Socket handle duplicated into child: %p", ChildSocket);

    ResumeThread(ProcessInfo.hThread);

    if (!ConnectNamedPipe(PipeHandle, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
        LOGERR("[Parent] ConnectNamedPipe failed: %u", GetLastError());
        goto Exit;
    }

    ChildParams.SocketHandle = (UINT64)(ULONG_PTR)ChildSocket;
    ChildParams.IfIndex = IfIndex;
    ChildParams.TimeoutSeconds = TimeoutSeconds;

    if (!WriteFile(PipeHandle, &ChildParams, sizeof(ChildParams), &BytesWritten, NULL) ||
        BytesWritten != sizeof(ChildParams)) {
        LOGERR("[Parent] WriteFile to pipe failed: %u", GetLastError());
        goto Exit;
    }

    CloseHandle(PipeHandle);
    PipeHandle = INVALID_HANDLE_VALUE;

    LOGINFO("[Parent] Parameters sent to child, waiting for child to exit...");

    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);

    {
        DWORD ChildExitCode = 1;
        GetExitCodeProcess(ProcessInfo.hProcess, &ChildExitCode);
        LOGINFO("[Parent] Child exited with code %u", ChildExitCode);
        ExitCode = (INT)ChildExitCode;
    }

Exit:
    if (ProcessCreated) {
        CloseHandle(ProcessInfo.hProcess);
        CloseHandle(ProcessInfo.hThread);
    }
    if (PipeHandle != INVALID_HANDLE_VALUE) {
        _Analysis_assume_(PipeHandle != NULL);
        CloseHandle(PipeHandle);
    }

    bpf_xdp_detach(IfIndex, 0, NULL);

    if (BpfObject != NULL) {
        bpf_object__close(BpfObject);
    }
    if (Socket != NULL) {
        CloseHandle(Socket);
    }
    if (RestrictedToken != NULL) {
        CloseHandle(RestrictedToken);
    }
    if (CurrentToken != NULL) {
        CloseHandle(CurrentToken);
    }
    if (AdminSid != NULL) {
        LocalFree(AdminSid);
    }

    return ExitCode;
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    if (argc >= 3 && _stricmp(argv[1], "-child") == 0) {
        WCHAR PipeName[128];

        if (MultiByteToWideChar(
                CP_ACP, 0, argv[2], -1, PipeName, ARRAYSIZE(PipeName)) == 0) {
            LOGERR("Invalid pipe name");
            return 1;
        }

        return RunChild(PipeName);
    }

    if (argc >= 2 && argv[1][0] != '-') {
        UINT32 IfIndex = (UINT32)atoi(argv[1]);
        const CHAR *BpfProgramPath = "xskrestricted_redirect.sys";
        const CHAR *ProgramName = "xskrestricted_redirect";
        UINT32 TimeoutSeconds = 0;

        if (IfIndex == 0) {
            LOGERR("Invalid IfIndex");
            return 1;
        }

        for (INT i = 2; i < argc - 1; i++) {
            if (_stricmp(argv[i], "-BpfProgram") == 0) {
                BpfProgramPath = argv[i + 1];
                i++;
            } else if (_stricmp(argv[i], "-ProgramName") == 0) {
                ProgramName = argv[i + 1];
                i++;
            } else if (_stricmp(argv[i], "-TimeoutSeconds") == 0) {
                TimeoutSeconds = (UINT32)atoi(argv[i + 1]);
                i++;
            }
        }

        return RunParent(IfIndex, BpfProgramPath, ProgramName, TimeoutSeconds);
    }

    fprintf(stderr, UsageText);

    return 1;
}
