//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This sample demonstrates how to create an AF_XDP socket and XDP program in
// a privileged parent process, then duplicate those handles into a child
// process running with a restricted token. The child process uses the
// duplicated handles to perform RX-to-TX forwarding without needing elevated
// privileges itself.
//
// Approach:
//   1. Parent creates AF_XDP socket and XDP program (requires admin).
//   2. Parent creates a restricted token (Administrators SID disabled).
//   3. Parent spawns a child process (suspended) with the restricted token.
//   4. Parent duplicates the socket and program handles into the child.
//   5. Parent writes handle values to a named pipe the child reads on startup.
//   6. Parent resumes the child, which configures rings and forwards packets.
//
// Usage:
//   xskrestricted.exe <IfIndex> [-TimeoutSeconds <Seconds>]
//

#include <xdpapi.h>
#include <afxdp_helper.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

#define LOGINFO(...) \
    fprintf(stdout, "INFO: "); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n")

static const XDP_HOOK_ID XdpInspectRxL2 = {
    .Layer = XDP_HOOK_L2,
    .Direction = XDP_HOOK_RX,
    .SubLayer = XDP_HOOK_INSPECT,
};

//
// A simple structure passed from the parent to the child via a named pipe.
// Contains the duplicated handle values in the child's handle table.
//
typedef struct _XSKRESTRICTED_CHILD_PARAMS {
    UINT64 SocketHandle;
    UINT64 ProgramHandle;
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
    HANDLE Program;
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

    //
    // Connect to the named pipe created by the parent to receive the
    // duplicated handle values.
    //
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
    Program = (HANDLE)(ULONG_PTR)Params.ProgramHandle;

    LOGINFO(
        "[Child] Running with duplicated handles: Socket=%p Program=%p IfIndex=%u",
        Socket, Program, Params.IfIndex);

    //
    // Register our frame buffer with the AF_XDP socket. The parent created
    // and bound the socket, but UMEM registration, ring setup, and activation
    // are done here in the child's address space.
    //
    UmemReg.TotalSize = sizeof(Frame);
    UmemReg.ChunkSize = sizeof(Frame);
    UmemReg.Address = Frame;

    Result = XskSetSockopt(Socket, XSK_SOCKOPT_UMEM_REG, &UmemReg, sizeof(UmemReg));
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XSK_UMEM_REG failed: %x", Result);
        return 1;
    }

    //
    // Set ring sizes.
    //
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

    //
    // Activate the socket so rings become available.
    //
    Result = XskActivate(Socket, XSK_ACTIVATE_FLAG_NONE);
    if (XDP_FAILED(Result)) {
        LOGERR("[Child] XskActivate failed: %x", Result);
        return 1;
    }

    //
    // Retrieve ring info and initialize ring helpers.
    //
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

    //
    // RX-to-TX forwarding loop: receive frames, swap MAC addresses, and
    // transmit them back. If a timeout was specified, exit after the deadline.
    //
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

            //
            // Swap source and destination MAC addresses to echo the frame
            // back to the sender.
            //
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
    CloseHandle(Program);

    return 0;
}

//
// Parent process: create the AF_XDP socket and XDP program, create a
// restricted child process, duplicate the handles into it, and communicate
// the handle values via a named pipe.
//
static
INT
RunParent(
    _In_ UINT32 IfIndex,
    _In_ UINT32 TimeoutSeconds
    )
{
    HRESULT Result;
    HANDLE Socket = NULL;
    HANDLE Program = NULL;
    XDP_RULE Rule = {0};
    HANDLE RestrictedToken = NULL;
    HANDLE CurrentToken = NULL;
    SID_AND_ATTRIBUTES SidsToDisable[1];
    DWORD SidSize;
    PSID AdminSid = NULL;
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    WCHAR CommandLine[512];
    HANDLE ChildSocket = NULL;
    HANDLE ChildProgram = NULL;
    INT ExitCode = 1;
    WCHAR ModulePathW[MAX_PATH];
    HANDLE PipeHandle = INVALID_HANDLE_VALUE;
    WCHAR PipeName[128];
    XSKRESTRICTED_CHILD_PARAMS ChildParams;
    DWORD BytesWritten;
    SECURITY_ATTRIBUTES PipeSa;
    BOOL ProcessCreated = FALSE;

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    LOGINFO("[Parent] Creating AF_XDP socket and XDP program for IfIndex=%u", IfIndex);

    //
    // Create a named pipe with a unique name based on the current process ID.
    // The child will read its handle values from this pipe on startup.
    //
    _snwprintf_s(
        PipeName, ARRAYSIZE(PipeName), _TRUNCATE,
        L"\\\\.\\pipe\\xskrestricted_%u", GetCurrentProcessId());

    //
    // Allow the restricted child to read the pipe by granting Everyone access.
    //
    ZeroMemory(&PipeSa, sizeof(PipeSa));
    PipeSa.nLength = sizeof(PipeSa);
    PipeSa.bInheritHandle = FALSE;

    PipeHandle =
        CreateNamedPipeW(
            PipeName,
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_BYTE | PIPE_WAIT,
            1,                      // Max instances
            sizeof(XSKRESTRICTED_CHILD_PARAMS),
            0,                      // In buffer size
            0,                      // Default timeout
            &PipeSa);
    if (PipeHandle == INVALID_HANDLE_VALUE) {
        LOGERR("[Parent] CreateNamedPipeW failed: %u", GetLastError());
        goto Exit;
    }

    //
    // Create an AF_XDP socket.
    //
    Result = XskCreate(&Socket);
    if (XDP_FAILED(Result)) {
        LOGERR("[Parent] XskCreate failed: %x", Result);
        goto Exit;
    }

    //
    // Bind the socket to the specified interface and 0th queue.
    //
    Result = XskBind(Socket, IfIndex, 0, XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX);
    if (XDP_FAILED(Result)) {
        LOGERR("[Parent] XskBind failed: %x", Result);
        goto Exit;
    }

    LOGINFO("[Parent] Socket created and bound (handle=%p)", Socket);

    //
    // Create an XDP program that redirects UDP port 1234 traffic to the socket.
    //
    Rule.Match = XDP_MATCH_UDP_DST;
    Rule.Pattern.Port = _byteswap_ushort(1234);
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Socket;

    Result = XdpCreateProgram(IfIndex, &XdpInspectRxL2, 0, 0, &Rule, 1, &Program);
    if (XDP_FAILED(Result)) {
        LOGERR("[Parent] XdpCreateProgram failed: %x", Result);
        goto Exit;
    }

    LOGINFO("[Parent] XDP program created (handle=%p)", Program);

    //
    // Create a restricted token by disabling the Administrators group SID.
    // This demonstrates privilege reduction: the child process cannot create
    // new XDP objects but can use pre-existing duplicated handles.
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
            CurrentToken,
            0,                      // Flags
            1,                      // DisableSidCount
            SidsToDisable,          // SidsToDisable
            0,                      // DeletePrivilegeCount
            NULL,                   // PrivilegesToDelete
            0,                      // RestrictedSidCount
            NULL,                   // SidsToRestrict
            &RestrictedToken)) {
        LOGERR("[Parent] CreateRestrictedToken failed: %u", GetLastError());
        goto Exit;
    }

    LOGINFO("[Parent] Restricted token created (Administrators SID disabled)");

    //
    // Create the child process in a suspended state. We pass the pipe name
    // on the command line so the child can read its handle values.
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
            RestrictedToken,
            NULL,                   // Application name (use command line)
            CommandLine,            // Command line
            NULL,                   // Process attributes
            NULL,                   // Thread attributes
            FALSE,                  // Inherit handles
            CREATE_SUSPENDED,       // Creation flags
            NULL,                   // Environment
            NULL,                   // Current directory
            &StartupInfo,
            &ProcessInfo)) {
        LOGERR("[Parent] CreateProcessAsUserW failed: %u", GetLastError());
        goto Exit;
    }

    ProcessCreated = TRUE;

    LOGINFO("[Parent] Child process created (PID=%u), suspended", ProcessInfo.dwProcessId);

    //
    // Duplicate the AF_XDP socket handle into the child process.
    //
    if (!DuplicateHandle(
            GetCurrentProcess(),    // Source process
            Socket,                 // Source handle
            ProcessInfo.hProcess,   // Target process
            &ChildSocket,           // Target handle (value in child's table)
            0,                      // Desired access (ignored with DUPLICATE_SAME_ACCESS)
            FALSE,                  // Inherit handle
            DUPLICATE_SAME_ACCESS)) {
        LOGERR("[Parent] DuplicateHandle(Socket) failed: %u", GetLastError());
        goto Exit;
    }

    //
    // Duplicate the XDP program handle into the child process.
    //
    if (!DuplicateHandle(
            GetCurrentProcess(),
            Program,
            ProcessInfo.hProcess,
            &ChildProgram,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS)) {
        LOGERR("[Parent] DuplicateHandle(Program) failed: %u", GetLastError());
        goto Exit;
    }

    LOGINFO(
        "[Parent] Handles duplicated into child: Socket=%p Program=%p",
        ChildSocket, ChildProgram);

    //
    // Resume the child process. It will connect to the named pipe and read
    // its parameters.
    //
    ResumeThread(ProcessInfo.hThread);

    //
    // Wait for the child to connect, then send the handle values.
    //
    if (!ConnectNamedPipe(PipeHandle, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
        LOGERR("[Parent] ConnectNamedPipe failed: %u", GetLastError());
        goto Exit;
    }

    ChildParams.SocketHandle = (UINT64)(ULONG_PTR)ChildSocket;
    ChildParams.ProgramHandle = (UINT64)(ULONG_PTR)ChildProgram;
    ChildParams.IfIndex = IfIndex;
    ChildParams.TimeoutSeconds = TimeoutSeconds;

    if (!WriteFile(PipeHandle, &ChildParams, sizeof(ChildParams), &BytesWritten, NULL) ||
        BytesWritten != sizeof(ChildParams)) {
        LOGERR("[Parent] WriteFile to pipe failed: %u", GetLastError());
        goto Exit;
    }

    //
    // Close the pipe; the child has its data.
    //
    CloseHandle(PipeHandle);
    PipeHandle = INVALID_HANDLE_VALUE;

    LOGINFO("[Parent] Parameters sent to child, waiting for child to exit...");

    //
    // Wait for the child process to exit.
    //
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
    if (Program != NULL) {
        CloseHandle(Program);
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
        //
        // Child mode: receive the pipe name, read handle values from the pipe.
        //
        WCHAR PipeName[128];

        if (MultiByteToWideChar(
                CP_ACP, 0, argv[2], -1, PipeName, ARRAYSIZE(PipeName)) == 0) {
            LOGERR("Invalid pipe name");
            return 1;
        }

        return RunChild(PipeName);
    }

    if (argc >= 2 && argv[1][0] != '-') {
        //
        // Parent mode: create XDP objects and spawn restricted child.
        //
        UINT32 IfIndex = (UINT32)atoi(argv[1]);
        UINT32 TimeoutSeconds = 0;

        if (IfIndex == 0) {
            LOGERR("Invalid IfIndex");
            return 1;
        }

        //
        // Check for optional -TimeoutSeconds argument.
        //
        for (INT i = 2; i < argc - 1; i++) {
            if (_stricmp(argv[i], "-TimeoutSeconds") == 0) {
                TimeoutSeconds = (UINT32)atoi(argv[i + 1]);
                break;
            }
        }

        return RunParent(IfIndex, TimeoutSeconds);
    }

    fprintf(
        stderr,
        "Usage:\n"
        "  xskrestricted.exe <IfIndex> [-TimeoutSeconds <Seconds>]\n"
        "\n"
        "Creates an AF_XDP socket and XDP program, then spawns a child process\n"
        "with a restricted token and duplicates the handles into it. The child\n"
        "forwards UDP port 1234 traffic back to the sender.\n"
        "\n"
        "Options:\n"
        "  -TimeoutSeconds <Seconds>  Exit after the specified number of seconds.\n"
        "                             If 0 or omitted, run indefinitely.\n");

    return 1;
}
