//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <winioctl.h>
#include <CppUnitTest.h>
#include <xdpapi.h>
#include <fntrace.h>

#include "xdpfunctionaltestdrvioctl.h"
#include "xdptest.h"
#include "tests.h"
#include "util.h"
#include "tests.tmh"

//
// Test suite(s).
//

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

class DriverService {
    SC_HANDLE ScmHandle;
    SC_HANDLE ServiceHandle;
public:
    DriverService() :
        ScmHandle(nullptr),
        ServiceHandle(nullptr) {
    }
    bool Initialize(
        _In_z_ const char* DriverName,
        _In_opt_z_ const char* DependentFileNames,
        _In_opt_z_ const char* DriverPath
        ) {
        uint32_t Error;
        ScmHandle = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (ScmHandle == nullptr) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "GetFullPathName failed");
            return false;
        }
    QueryService:
        ServiceHandle =
            OpenServiceA(
                ScmHandle,
                DriverName,
                SERVICE_ALL_ACCESS);
        if (ServiceHandle == nullptr) {
            TraceError(
                "[ lib] ERROR, %u, %s.",
                 GetLastError(),
                "OpenService failed");
            char DriverFilePath[MAX_PATH] = {0};
            char* PathEnd;
            if (DriverPath != nullptr) {
                strcpy_s(DriverFilePath, RTL_NUMBER_OF(DriverFilePath), DriverPath);
            } else {
                if (GetModuleFileNameA(NULL, DriverFilePath, MAX_PATH) == 0) {
                    TraceError(
                        "[ lib] ERROR, %s.",
                        "Failed to get currently executing module path");
                    return false;
                }
            }
            PathEnd = strrchr(DriverFilePath, '\\');
            if (PathEnd == NULL) {
                TraceError(
                    "[ lib] ERROR, %s.",
                    "Failed parsing unexpected module path format");
                return false;
            }
            PathEnd++;
            size_t RemainingLength = sizeof(DriverFilePath) - (PathEnd - DriverFilePath);
            int PathResult =
                snprintf(
                    PathEnd,
                    RemainingLength,
                    "%s.sys",
                    DriverName);
            if (PathResult <= 0 || (size_t)PathResult > RemainingLength) {
                TraceError(
                    "[ lib] ERROR, %s.",
                    "Failed to create driver on disk file path");
                return false;
            }
            if (GetFileAttributesA(DriverFilePath) == INVALID_FILE_ATTRIBUTES) {
                TraceError(
                    "[ lib] ERROR, %s.",
                    "Failed to find driver on disk");
                return false;
            }
            ServiceHandle =
                CreateServiceA(
                    ScmHandle,
                    DriverName,
                    DriverName,
                    SC_MANAGER_ALL_ACCESS,
                    SERVICE_KERNEL_DRIVER,
                    SERVICE_DEMAND_START,
                    SERVICE_ERROR_NORMAL,
                    DriverFilePath,
                    nullptr,
                    nullptr,
                    DependentFileNames,
                    nullptr,
                    nullptr);
            if (ServiceHandle == nullptr) {
                Error = GetLastError();
                if (Error == ERROR_SERVICE_EXISTS) {
                    goto QueryService;
                }
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "CreateService failed");
                return false;
            }
        }
        return true;
    }
    void Uninitialize() {
        if (ServiceHandle != nullptr) {
            CloseServiceHandle(ServiceHandle);
        }
        if (ScmHandle != nullptr) {
            CloseServiceHandle(ScmHandle);
        }
    }
    bool Start() {
        if (!StartServiceA(ServiceHandle, 0, nullptr)) {
            uint32_t Error = GetLastError();
            if (Error != ERROR_SERVICE_ALREADY_RUNNING) {
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "StartService failed");
                return false;
            }
        }
        return true;
    }
};

class DriverClient {
    HANDLE DeviceHandle;
public:
    DriverClient() : DeviceHandle(INVALID_HANDLE_VALUE) { }
    ~DriverClient() { Uninitialize(); }
    bool Initialize(
        _In_z_ const char* DriverName
        ) {
        uint32_t Error;
        char IoctlPath[MAX_PATH];
        int PathResult =
            snprintf(
                IoctlPath,
                sizeof(IoctlPath),
                "\\\\.\\\\%s",
                DriverName);
        if (PathResult < 0 || PathResult >= sizeof(IoctlPath)) {
            TraceError(
                "[ lib] ERROR, %s.",
                "Creating Driver File Path failed");
            return false;
        }
        DeviceHandle =
            CreateFileA(
                IoctlPath,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,                // no SECURITY_ATTRIBUTES structure
                OPEN_EXISTING,          // No special create flags
                FILE_FLAG_OVERLAPPED,   // Allow asynchronous requests
                nullptr);
        if (DeviceHandle == INVALID_HANDLE_VALUE) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "CreateFile failed");
            return false;
        }
        return true;
    }
    void Uninitialize() {
        if (DeviceHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(DeviceHandle);
            DeviceHandle = INVALID_HANDLE_VALUE;
        }
    }
    bool Run(
        _In_ uint32_t IoControlCode,
        _In_reads_bytes_opt_(InBufferSize)
            void* InBuffer,
        _In_ uint32_t InBufferSize,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        uint32_t Error;
        OVERLAPPED Overlapped = { 0 };
        Overlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (Overlapped.hEvent == nullptr) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "CreateEvent failed");
            return false;
        }
        TraceVerbose(
            "[test] Sending Write IOCTL %u with %u bytes.",
            IoControlCode,
            InBufferSize);
        if (!DeviceIoControl(
                DeviceHandle,
                IoControlCode,
                InBuffer, InBufferSize,
                nullptr, 0,
                nullptr,
                &Overlapped)) {
            Error = GetLastError();
            if (Error != ERROR_IO_PENDING) {
                CloseHandle(Overlapped.hEvent);
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "DeviceIoControl Write failed");
                return false;
            }
        }
        DWORD dwBytesReturned;
        if (!GetOverlappedResultEx(
                DeviceHandle,
                &Overlapped,
                &dwBytesReturned,
                TimeoutMs,
                FALSE)) {
            Error = GetLastError();
            if (Error == WAIT_TIMEOUT) {
                Error = ERROR_TIMEOUT;
                CancelIoEx(DeviceHandle, &Overlapped);
            }
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "GetOverlappedResultEx Write failed");
        } else {
            Error = ERROR_SUCCESS;
        }
        CloseHandle(Overlapped.hEvent);
        return Error == ERROR_SUCCESS;
    }
    bool Run(
        _In_ uint32_t IoControlCode,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        return Run(IoControlCode, nullptr, 0, TimeoutMs);
    }
    template<class T>
    bool Run(
        _In_ uint32_t IoControlCode,
        _In_ const T& Data,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        return Run(IoControlCode, (void*)&Data, sizeof(Data), TimeoutMs);
    }
    _Success_(return)
    bool Read(
        _In_ uint32_t IoControlCode,
        _Out_writes_bytes_opt_(OutBufferSize)
            void* OutBuffer,
        _In_ uint32_t OutBufferSize,
        _Out_opt_ uint32_t* OutBufferWritten,
        _In_ uint32_t TimeoutMs = 30000
        ) {
        uint32_t Error;
        OVERLAPPED Overlapped = { 0 };
        Overlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!Overlapped.hEvent) {
            Error = GetLastError();
            TraceError(
                "[ lib] ERROR, %u, %s.",
                Error,
                "CreateEvent failed");
            return false;
        }
        TraceVerbose(
            "[test] Sending Read IOCTL %u.",
            IoControlCode);
        if (!DeviceIoControl(
                DeviceHandle,
                IoControlCode,
                nullptr, 0,
                OutBuffer, OutBufferSize,
                nullptr,
                &Overlapped)) {
            Error = GetLastError();
            if (Error != ERROR_IO_PENDING) {
                CloseHandle(Overlapped.hEvent);
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "DeviceIoControl Write failed");
                return false;
            }
        }
        DWORD dwBytesReturned;
        if (!GetOverlappedResultEx(
                DeviceHandle,
                &Overlapped,
                &dwBytesReturned,
                TimeoutMs,
                FALSE)) {
            Error = GetLastError();
            if (Error == WAIT_TIMEOUT) {
                Error = ERROR_TIMEOUT;
                if (CancelIoEx(DeviceHandle, &Overlapped)) {
                    GetOverlappedResult(DeviceHandle, &Overlapped, &dwBytesReturned, true);
                }
            } else {
                TraceError(
                    "[ lib] ERROR, %u, %s.",
                    Error,
                    "GetOverlappedResultEx Read failed");
            }
        } else {
            Error = ERROR_SUCCESS;
            if (OutBufferWritten != NULL) {
                *OutBufferWritten = dwBytesReturned;
            }
        }
        CloseHandle(Overlapped.hEvent);
        return Error == ERROR_SUCCESS;
    }
};

static bool TestingKernelMode = false;
static DriverService TestDriverService;
static DriverClient TestDriverClient;

VOID
StopTest()
{
    Assert::Fail(L"Stop test execution.");
}

VOID
LogTestFailure(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Line);

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    TraceError("%S", Buffer);
    Logger::WriteMessage(Buffer);
}

VOID
LogTestWarning(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Line);

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    TraceWarn("%S", Buffer);
    Logger::WriteMessage(Buffer);
}

TEST_MODULE_INITIALIZE(ModuleSetup)
{
    size_t RequiredSize;

    WPP_INIT_TRACING(NULL);

    getenv_s(&RequiredSize, NULL, 0, "xdpfunctionaltests::KernelModeEnabled");
    TestingKernelMode = (RequiredSize != 0);

    TraceInfo(
        "[ lib] INFO, KernelMode=%d",
        TestingKernelMode);
    if (TestingKernelMode) {
        char DriverPath[MAX_PATH];

        TEST_EQUAL(
            0,
            getenv_s(
                &RequiredSize, DriverPath, RTL_NUMBER_OF(DriverPath),
                "xdpfunctionaltests::KernelModeDriverPath"));
        TEST_TRUE(TestDriverService.Initialize(FUNCTIONAL_TEST_DRIVER_NAME, nullptr, DriverPath));
        TEST_TRUE(TestDriverService.Start());

        TEST_TRUE(TestDriverClient.Initialize(FUNCTIONAL_TEST_DRIVER_NAME));
    } else {
        Assert::IsTrue(TestSetup());
    }
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    if (TestingKernelMode) {
        TestDriverClient.Uninitialize();
        TestDriverService.Uninitialize();
    } else {
        Assert::IsTrue(TestCleanup());
    }
    WPP_CLEANUP();
}

TEST_CLASS(xdpfunctionaltests)
{
public:
    TEST_METHOD(OpenApi) {
        if (!TestingKernelMode) {
            ::OpenApiTest();
        }
    }

    TEST_METHOD(LoadApi) {
        if (!TestingKernelMode) {
            ::LoadApiTest();
        }
    }

    TEST_METHOD(GenericBinding) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_BINDING));
        } else {
            ::GenericBinding();
        }
    }

    TEST_METHOD(GenericBindingResetAdapter) {
        if (!TestingKernelMode) {
            ::GenericBindingResetAdapter();
        }
    }

    TEST_METHOD(GenericRxSingleFrame) {
        if (!TestingKernelMode) {
            ::GenericRxSingleFrame();
        }
    }

    TEST_METHOD(GenericRxNoPoke) {
        if (!TestingKernelMode) {
            ::GenericRxNoPoke();
        }
    }

    TEST_METHOD(GenericRxBackfillAndTrailer) {
        if (!TestingKernelMode) {
            ::GenericRxBackfillAndTrailer();
        }
    }

    TEST_METHOD(GenericRxLowResources) {
        if (!TestingKernelMode) {
            ::GenericRxLowResources();
        }
    }

    TEST_METHOD(GenericRxMultiSocket) {
        if (!TestingKernelMode) {
            ::GenericRxMultiSocket();
        }
    }

    TEST_METHOD(GenericRxMultiProgram) {
        if (!TestingKernelMode) {
            ::GenericRxMultiProgram();
        }
    }

    TEST_METHOD(GenericTxToRxInject) {
        if (!TestingKernelMode) {
            ::GenericTxToRxInject();
        }
    }

    TEST_METHOD(GenericTxSingleFrame) {
        if (!TestingKernelMode) {
            ::GenericTxSingleFrame();
        }
    }

    TEST_METHOD(GenericTxOutOfOrder) {
        if (!TestingKernelMode) {
            ::GenericTxOutOfOrder();
        }
    }

    TEST_METHOD(GenericTxSharing) {
        if (!TestingKernelMode) {
            ::GenericTxSharing();
        }
    }

    TEST_METHOD(GenericTxPoke) {
        if (!TestingKernelMode) {
            ::GenericTxPoke();
        }
    }

    TEST_METHOD(GenericTxMtu) {
        if (!TestingKernelMode) {
            ::GenericTxMtu();
        }
    }

    TEST_METHOD(GenericRxTcpControlV4) {
        if (!TestingKernelMode) {
            GenericRxTcpControl(AF_INET);
        }
    }

    TEST_METHOD(GenericRxTcpControlV6) {
        if (!TestingKernelMode) {
            GenericRxTcpControl(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxAllQueueRedirectV4) {
        if (!TestingKernelMode) {
            GenericRxAllQueueRedirect(AF_INET);
        }
    }

    TEST_METHOD(GenericRxAllQueueRedirectV6) {
        if (!TestingKernelMode) {
            GenericRxAllQueueRedirect(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxMatchUdpV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_UDP, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_UDP, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_UDP_DST, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_UDP_DST, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpTupleV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_IPV4_UDP_TUPLE, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpTupleV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_UDP_TUPLE, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicSrcV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicSrcV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicDstV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicDstV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchIpPrefixV4) {
        if (!TestingKernelMode) {
            GenericRxMatchIpPrefix(AF_INET);
        }
    }

    TEST_METHOD(GenericRxMatchIpPrefixV6) {
        if (!TestingKernelMode) {
            GenericRxMatchIpPrefix(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv4UdpPortSet) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_IPV4_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv6UdpPortSet) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv4TcpPortSet) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_IPV4_TCP_PORT_SET, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv6TcpPortSet) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_TCP_PORT_SET, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpPortV4) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET, XDP_MATCH_TCP_DST, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpPortV6) {
        if (!TestingKernelMode) {
            GenericRxMatch(AF_INET6, XDP_MATCH_TCP_DST, FALSE);
        }
    }

    TEST_METHOD(GenericXskWaitRx) {
        if (!TestingKernelMode) {
            GenericXskWait(TRUE, FALSE);
        }
    }

    TEST_METHOD(GenericXskWaitTx) {
        if (!TestingKernelMode) {
            GenericXskWait(FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericXskWaitRxTx) {
        if (!TestingKernelMode) {
            GenericXskWait(TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericXskWaitAsyncRx) {
        if (!TestingKernelMode) {
            GenericXskWaitAsync(TRUE, FALSE);
        }
    }

    TEST_METHOD(GenericXskWaitAsyncTx) {
        if (!TestingKernelMode) {
            GenericXskWaitAsync(FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericXskWaitAsyncRxTx) {
        if (!TestingKernelMode) {
            GenericXskWaitAsync(TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericLwfDelayDetachRx) {
        if (!TestingKernelMode) {
            GenericLwfDelayDetach(TRUE, FALSE);
        }
    }

    TEST_METHOD(GenericLwfDelayDetachTx) {
        if (!TestingKernelMode) {
            GenericLwfDelayDetach(FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericLwfDelayDetachRxTx) {
        if (!TestingKernelMode) {
            GenericLwfDelayDetach(TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicLongHeaderV4) {
        if (!TestingKernelMode) {
            GenericRxUdpFragmentQuicLongHeader(AF_INET, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicLongHeaderV6) {
        if (!TestingKernelMode) {
            GenericRxUdpFragmentQuicLongHeader(AF_INET6, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentQuicLongHeaderV4) {
        if (!TestingKernelMode) {
            GenericRxUdpFragmentQuicLongHeader(AF_INET, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentQuicLongHeaderV6) {
        if (!TestingKernelMode) {
            GenericRxUdpFragmentQuicLongHeader(AF_INET6, FALSE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV4) {
        if (!TestingKernelMode) {
            GenericRxUdpFragmentQuicShortHeader(AF_INET);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV6) {
        if (!TestingKernelMode) {
            GenericRxUdpFragmentQuicShortHeader(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV4) {
        if (!TestingKernelMode) {
            GenericRxFragmentHeaderData(AF_INET, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV6) {
        if (!TestingKernelMode) {
            GenericRxFragmentHeaderData(AF_INET6, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentHeaderDataV4) {
        if (!TestingKernelMode) {
            GenericRxFragmentHeaderData(AF_INET, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentHeaderDataV6) {
        if (!TestingKernelMode) {
            GenericRxFragmentHeaderData(AF_INET6, FALSE);
        }
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV4) {
        if (!TestingKernelMode) {
            GenericRxTooManyFragments(AF_INET, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV6) {
        if (!TestingKernelMode) {
            GenericRxTooManyFragments(AF_INET6, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpTooManyFragmentsV4) {
        if (!TestingKernelMode) {
            GenericRxTooManyFragments(AF_INET, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpTooManyFragmentsV6) {
        if (!TestingKernelMode) {
            GenericRxTooManyFragments(AF_INET6, FALSE);
        }
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV4) {
        if (!TestingKernelMode) {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV6) {
        if (!TestingKernelMode) {
            GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpHeaderFragmentsV4) {
        if (!TestingKernelMode) {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpHeaderFragmentsV6) {
        if (!TestingKernelMode) {
            GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, FALSE);
        }
    }

    TEST_METHOD(GenericRxL2Fwd) {
        if (!TestingKernelMode) {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE);
        }
    }

    TEST_METHOD(GenericRxL2FwdLowResources) {
        if (!TestingKernelMode) {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericRxL2FwdTxInspect) {
        if (!TestingKernelMode) {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericRxFromTxInspectV4) {
        if (!TestingKernelMode) {
            GenericRxFromTxInspect(AF_INET);
        }
    }

    TEST_METHOD(GenericRxFromTxInspectV6) {
        if (!TestingKernelMode) {
            GenericRxFromTxInspect(AF_INET6);
        }
    }

    TEST_METHOD(SecurityAdjustDeviceAcl) {
        if (!TestingKernelMode) {
            ::SecurityAdjustDeviceAcl();
        }
    }

    TEST_METHOD(GenericRxEbpfAttach) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfAttach();
        }
    }

    TEST_METHOD(GenericRxEbpfDrop) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfDrop();
        }
    }

    TEST_METHOD(GenericRxEbpfPass) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfPass();
        }
    }

    TEST_METHOD(GenericRxEbpfTx) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfTx();
        }
    }

    TEST_METHOD(GenericRxEbpfPayload) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfPayload();
        }
    }

    TEST_METHOD(ProgTestRunRxEbpfPayload) {
        if (!TestingKernelMode) {
            ::ProgTestRunRxEbpfPayload();
        }
    }

    TEST_METHOD(GenericRxEbpfIfIndex) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfIfIndex();
        }
    }

    TEST_METHOD(GenericRxEbpfFragments) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfFragments();
        }
    }

    TEST_METHOD(GenericRxEbpfUnload) {
        if (!TestingKernelMode) {
            ::GenericRxEbpfUnload();
        }
    }

    TEST_METHOD(GenericLoopbackV4) {
        if (!TestingKernelMode) {
            GenericLoopback(AF_INET);
        }
    }

    TEST_METHOD(GenericLoopbackV6) {
        if (!TestingKernelMode) {
            GenericLoopback(AF_INET6);
        }
    }

    TEST_METHOD(OffloadRssError) {
        if (!TestingKernelMode) {
            ::OffloadRssError();
        }
    }

    TEST_METHOD(OffloadRssReference) {
        if (!TestingKernelMode) {
            ::OffloadRssReference();
        }
    }

    TEST_METHOD(OffloadRssInterfaceRestart) {
        if (!TestingKernelMode) {
            ::OffloadRssInterfaceRestart();
        }
    }

    TEST_METHOD(OffloadRssUnchanged) {
        if (!TestingKernelMode) {
            ::OffloadRssUnchanged();
        }
    }

    TEST_METHOD(OffloadRssUpperSet) {
        if (!TestingKernelMode) {
            ::OffloadRssUpperSet();
        }
    }

    TEST_METHOD(OffloadRssSet) {
        if (!TestingKernelMode) {
            ::OffloadRssSet();
        }
    }

    TEST_METHOD(OffloadRssCapabilities) {
        if (!TestingKernelMode) {
            ::OffloadRssCapabilities();
        }
    }

    TEST_METHOD(OffloadRssReset) {
        if (!TestingKernelMode) {
            ::OffloadRssReset();
        }
    }

    TEST_METHOD(OffloadSetHardwareCapabilities) {
        if (!TestingKernelMode) {
            ::OffloadSetHardwareCapabilities();
        }
    }

    TEST_METHOD(GenericXskQueryAffinity) {
        if (!TestingKernelMode) {
            ::GenericXskQueryAffinity();
        }
    }

    TEST_METHOD(OffloadQeoConnection) {
        if (!TestingKernelMode) {
            ::OffloadQeoConnection();
        }
    }

    TEST_METHOD(OffloadQeoRevertInterfaceRemoval) {
        if (!TestingKernelMode) {
            ::OffloadQeoRevert(RevertReasonInterfaceRemoval);
        }
    }

    TEST_METHOD(OffloadQeoRevertHandleClosure) {
        if (!TestingKernelMode) {
            ::OffloadQeoRevert(RevertReasonHandleClosure);
        }
    }

    TEST_METHOD(OffloadQeoOidFailure) {
        if (!TestingKernelMode) {
            ::OffloadQeoOidFailure();
        }
    }
};
