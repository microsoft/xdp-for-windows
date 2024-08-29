//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <winioctl.h>

//
// Directly include some C++ headers that produce benign compiler warnings.
//
#pragma warning(push)
#pragma warning(disable:5252) // Multiple different types resulted in the same XFG type-hash D275361C54538B70; the PDB will only record information for one of them
#include <xlocnum>
#include <xlocale>
#pragma warning(pop)

#include <CppUnitTest.h>
#include <xdpapi.h>
#include <fntrace.h>

#include "xdpfunctionaltestdrvioctl.h"
#include "xdptest.h"
#include "tests.h"
#include "util.h"
#include "tests.tmh"

//
// Define a test method for a feature not yet officially released.
// Unfortunately, the vstest.console.exe runner seems unable to filter on
// arbitrary properties, so mark prerelease as priority 1.
//
#define TEST_METHOD_PRERELEASE(_Name) \
    BEGIN_TEST_METHOD_ATTRIBUTE(_Name) \
        TEST_PRIORITY(1) \
    END_TEST_METHOD_ATTRIBUTE() \
    TEST_METHOD(_Name)

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
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_LOAD_API));
        } else {
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
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_BINDING_RESET_ADAPTER));
        } else {
            ::GenericBindingResetAdapter();
        }
    }

    TEST_METHOD(GenericRxSingleFrame) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_SINGLE_FRAME));
        } else {
            ::GenericRxSingleFrame();
        }
    }

    TEST_METHOD(GenericRxNoPoke) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_NO_POKE));
        } else {
            ::GenericRxNoPoke();
        }
    }

    TEST_METHOD(GenericRxBackfillAndTrailer) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_BACKFILL_AND_TRAILER));
        } else {
            ::GenericRxBackfillAndTrailer();
        }
    }

    TEST_METHOD(GenericRxLowResources) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_LOW_RESOURCES));
        } else {
            ::GenericRxLowResources();
        }
    }

    TEST_METHOD(GenericRxMultiSocket) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MULTI_SOCKET));
        } else {
            ::GenericRxMultiSocket();
        }
    }

    TEST_METHOD(GenericRxMultiProgram) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MULTI_PROGRAM));
        } else {
            ::GenericRxMultiProgram();
        }
    }

    TEST_METHOD(GenericTxToRxInject) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_TX_TO_RX_INJECT));
        } else {
            ::GenericTxToRxInject();
        }
    }

    TEST_METHOD(GenericTxSingleFrame) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_TX_SINGLE_FRAME));
        } else {
            ::GenericTxSingleFrame();
        }
    }

    TEST_METHOD(GenericTxOutOfOrder) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_TX_OUT_OF_ORDER));
        } else {
            ::GenericTxOutOfOrder();
        }
    }

    TEST_METHOD(GenericTxSharing) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_TX_SHARING));
        } else {
            ::GenericTxSharing();
        }
    }

    TEST_METHOD(GenericTxPoke) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_TX_POKE));
        } else {
            ::GenericTxPoke();
        }
    }

    TEST_METHOD(GenericTxMtu) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_TX_MTU));
        } else {
            ::GenericTxMtu();
        }
    }

    TEST_METHOD(GenericRxTcpControlV4) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_CONTROL_V4, AF_INET));
        } else {
            GenericRxTcpControl(AF_INET);
        }
    }

    TEST_METHOD(GenericRxTcpControlV6) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_CONTROL_V6, AF_INET6));
        } else {
            GenericRxTcpControl(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxAllQueueRedirectV4) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_ALL_QUEUE_REDIRECT_V4, AF_INET));
        } else {
            GenericRxAllQueueRedirect(AF_INET);
        }
    }

    TEST_METHOD(GenericRxAllQueueRedirectV6) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_ALL_QUEUE_REDIRECT_V6, AF_INET6));
        } else {
            GenericRxAllQueueRedirect(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxMatchUdpV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_UDP, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_UDP, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_UDP, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_UDP, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_UDP_DST, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_PORT_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_UDP_DST, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_UDP_DST, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_PORT_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_UDP_DST, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpTupleV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_IPV4_UDP_TUPLE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_TUPLE_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_IPV4_UDP_TUPLE, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpTupleV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_IPV6_UDP_TUPLE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_TUPLE_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_UDP_TUPLE, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_QUIC_SRC_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_QUIC_SRC_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_QUIC_DST_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_QUIC_DST_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicSrcV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_TCP_QUIC_SRC_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicSrcV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_TCP_QUIC_SRC_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicDstV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_TCP_QUIC_DST_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpQuicDstV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_TCP_QUIC_DST_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchIpPrefixV4) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_IP_PREFIX_V4, AF_INET));
        } else {
            GenericRxMatchIpPrefix(AF_INET);
        }
    }

    TEST_METHOD(GenericRxMatchIpPrefixV6) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_IP_PREFIX_V6, AF_INET6));
        } else {
            GenericRxMatchIpPrefix(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_UDP_PORT_SET, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_PORT_SET_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_UDP_PORT_SET, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_UDP_PORT_SET_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv4UdpPortSet) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_IPV4_UDP_PORT_SET, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_IPV4_UDP_PORT_SET, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_IPV4_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv6UdpPortSet) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_IPV6_UDP_PORT_SET, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_IPV6_UDP_PORT_SET, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_UDP_PORT_SET, TRUE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv4TcpPortSet) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_IPV4_TCP_PORT_SET, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_IPV4_TCP_PORT_SET, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_IPV4_TCP_PORT_SET, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchIpv6TcpPortSet) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_IPV6_TCP_PORT_SET, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_IPV6_TCP_PORT_SET, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_TCP_PORT_SET, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpPortV4) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET, XDP_MATCH_TCP_DST, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_TCP_PORT_V4, Params));
        } else {
            GenericRxMatch(AF_INET, XDP_MATCH_TCP_DST, FALSE);
        }
    }

    TEST_METHOD(GenericRxMatchTcpPortV6) {
        if (TestingKernelMode) {
            GENERIC_RX_MATCH_PARAMS Params = { AF_INET6, XDP_MATCH_TCP_DST, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_MATCH_TCP_PORT_V6, Params));
        } else {
            GenericRxMatch(AF_INET6, XDP_MATCH_TCP_DST, FALSE);
        }
    }

    TEST_METHOD(GenericXskWaitRx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { TRUE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_XSK_WAIT_RX, Params));
        } else {
            GenericXskWait(TRUE, FALSE);
        }
    }

    TEST_METHOD(GenericXskWaitTx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { FALSE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_XSK_WAIT_TX, Params));
        } else {
            GenericXskWait(FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericXskWaitRxTx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { TRUE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_XSK_WAIT_RX_TX, Params));
        } else {
            GenericXskWait(TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericXskWaitAsyncRx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { TRUE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_XSK_WAIT_ASYNC_RX, Params));
        } else {
            GenericXskWaitAsync(TRUE, FALSE);
        }
    }

    TEST_METHOD(GenericXskWaitAsyncTx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { FALSE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_XSK_WAIT_ASYNC_TX, Params));
        } else {
            GenericXskWaitAsync(FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericXskWaitAsyncRxTx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { TRUE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_XSK_WAIT_ASYNC_RX_TX, Params));
        } else {
            GenericXskWaitAsync(TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericLwfDelayDetachRx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { TRUE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_LWF_DELAY_DETACH_RX, Params));
        } else {
            GenericLwfDelayDetach(TRUE, FALSE);
        }
    }

    TEST_METHOD(GenericLwfDelayDetachTx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { FALSE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_LWF_DELAY_DETACH_TX, Params));
        } else {
            GenericLwfDelayDetach(FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericLwfDelayDetachRxTx) {
        if (TestingKernelMode) {
            GENERIC_XSK_WAIT_PARAMS Params = { TRUE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_LWF_DELAY_DETACH_RX_TX, Params));
        } else {
            GenericLwfDelayDetach(TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicLongHeaderV4) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_FRAGMENT_QUIC_LONG_HEADER_V4, Params));
        } else {
            GenericRxUdpFragmentQuicLongHeader(AF_INET, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicLongHeaderV6) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET6, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_FRAGMENT_QUIC_LONG_HEADER_V6, Params));
        } else {
            GenericRxUdpFragmentQuicLongHeader(AF_INET6, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentQuicLongHeaderV4) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_FRAGMENT_QUIC_LONG_HEADER_V4, Params));
        } else {
            GenericRxUdpFragmentQuicLongHeader(AF_INET, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentQuicLongHeaderV6) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET6, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_FRAGMENT_QUIC_LONG_HEADER_V6, Params));
        } else {
            GenericRxUdpFragmentQuicLongHeader(AF_INET6, FALSE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV4) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_FRAGMENT_QUIC_SHORT_HEADER_V4, AF_INET));
        } else {
            GenericRxUdpFragmentQuicShortHeader(AF_INET);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV6) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_FRAGMENT_QUIC_SHORT_HEADER_V6, AF_INET6));
        } else {
            GenericRxUdpFragmentQuicShortHeader(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV4) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_FRAGMENT_HEADER_DATA_V4, Params));
        } else {
            GenericRxFragmentHeaderData(AF_INET, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV6) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET6, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_FRAGMENT_HEADER_DATA_V6, Params));
        } else {
            GenericRxFragmentHeaderData(AF_INET6, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentHeaderDataV4) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_FRAGMENT_HEADER_DATA_V4, Params));
        } else {
            GenericRxFragmentHeaderData(AF_INET, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpFragmentHeaderDataV6) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET6, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_FRAGMENT_HEADER_DATA_V6, Params));
        } else {
            GenericRxFragmentHeaderData(AF_INET6, FALSE);
        }
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV4) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_TOO_MANY_FRAGMENTS_V4, Params));
        } else {
            GenericRxTooManyFragments(AF_INET, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV6) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET6, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_TOO_MANY_FRAGMENTS_V6, Params));
        } else {
            GenericRxTooManyFragments(AF_INET6, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpTooManyFragmentsV4) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_TOO_MANY_FRAGMENTS_V4, Params));
        } else {
            GenericRxTooManyFragments(AF_INET, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpTooManyFragmentsV6) {
        if (TestingKernelMode) {
            GENERIC_RX_FRAGMENT_PARAMS Params = { AF_INET6, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_TOO_MANY_FRAGMENTS_V6, Params));
        } else {
            GenericRxTooManyFragments(AF_INET6, FALSE);
        }
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV4) {
        if (TestingKernelMode) {
            GENERIC_RX_PARAMS Params = { AF_INET, XDP_PROGRAM_ACTION_REDIRECT, TRUE, FALSE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_HEADER_FRAGMENTS_V4, Params));
        } else {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, TRUE);
        }
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV6) {
        if (TestingKernelMode) {
            GENERIC_RX_PARAMS Params = { AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, TRUE, FALSE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_UDP_HEADER_FRAGMENTS_V6, Params));
        } else {
            GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, TRUE);
        }
    }

    TEST_METHOD(GenericRxTcpHeaderFragmentsV4) {
        if (TestingKernelMode) {
            GENERIC_RX_PARAMS Params = { AF_INET, XDP_PROGRAM_ACTION_REDIRECT, FALSE, FALSE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_HEADER_FRAGMENTS_V4, Params));
        } else {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, FALSE);
        }
    }

    TEST_METHOD(GenericRxTcpHeaderFragmentsV6) {
        if (TestingKernelMode) {
            GENERIC_RX_PARAMS Params = { AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, FALSE, FALSE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_TCP_HEADER_FRAGMENTS_V6, Params));
        } else {
            GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, FALSE);
        }
    }

    TEST_METHOD(GenericRxL2Fwd) {
        if (TestingKernelMode) {
            GENERIC_RX_PARAMS Params = { AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, FALSE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_L2_FWD, Params));
        } else {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE);
        }
    }

    TEST_METHOD(GenericRxL2FwdLowResources) {
        if (TestingKernelMode) {
            GENERIC_RX_PARAMS Params = { AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, FALSE, TRUE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_L2_FWD_LOW_RESOURCES, Params));
        } else {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, FALSE, TRUE);
        }
    }

    TEST_METHOD(GenericRxL2FwdTxInspect) {
        if (TestingKernelMode) {
            GENERIC_RX_PARAMS Params = { AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, TRUE, FALSE };
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_L2_FWD_TX_INSPECT, Params));
        } else {
            GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, TRUE);
        }
    }

    TEST_METHOD(GenericRxFromTxInspectV4) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_FROM_TX_INSPECT_V4, AF_INET));
        } else {
            GenericRxFromTxInspect(AF_INET);
        }
    }

    TEST_METHOD(GenericRxFromTxInspectV6) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_FROM_TX_INSPECT_V6, AF_INET6));
        } else {
            GenericRxFromTxInspect(AF_INET6);
        }
    }

    TEST_METHOD(GenericRxForwardGroSanityV4) {
        GenericRxForwardGroSanity(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroSanityV6) {
        GenericRxForwardGroSanity(AF_INET6);
    }

    TEST_METHOD(GenericRxForwardGroMdlOffsetsV4) {
        GenericRxForwardGroMdlOffsets(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroMdlOffsetsV6) {
        GenericRxForwardGroMdlOffsets(AF_INET6);
    }

    TEST_METHOD(GenericRxForwardGroPureAckV4) {
        GenericRxForwardGroPureAck(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroPureAckV6) {
        GenericRxForwardGroPureAck(AF_INET6);
    }

    TEST_METHOD(GenericRxForwardGroDataTrailerV4) {
        GenericRxForwardGroDataTrailer(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroDataTrailerV6) {
        GenericRxForwardGroDataTrailer(AF_INET6);
    }

    TEST_METHOD(GenericRxForwardGroTcpOptionsV4) {
        GenericRxForwardGroTcpOptions(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroTcpOptionsV6) {
        GenericRxForwardGroTcpOptions(AF_INET6);
    }

    TEST_METHOD(GenericRxForwardGroMtuV4) {
        GenericRxForwardGroMtu(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroMtuV6) {
        GenericRxForwardGroMtu(AF_INET6);
    }

    TEST_METHOD(GenericRxForwardGroMaxOffloadV4) {
        GenericRxForwardGroMaxOffload(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroMaxOffloadV6) {
        GenericRxForwardGroMaxOffload(AF_INET6);
    }


    TEST_METHOD(GenericRxForwardGroTcpFlagsV4) {
        GenericRxForwardGroTcpFlags(AF_INET);
    }

    TEST_METHOD(GenericRxForwardGroTcpFlagsV6) {
        GenericRxForwardGroTcpFlags(AF_INET6);
    }

    TEST_METHOD(GenericRxFuzzForwardGroV4) {
        GenericRxFuzzForwardGro(AF_INET);
    }

    TEST_METHOD(GenericRxFuzzForwardGroV6) {
        GenericRxFuzzForwardGro(AF_INET6);
    }

    TEST_METHOD(SecurityAdjustDeviceAcl) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_SECURITY_ADJUST_DEVICE_ACL));
        } else {
            ::SecurityAdjustDeviceAcl();
        }
    }

    TEST_METHOD(EbpfNetsh) {
        ::EbpfNetsh();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfAttach) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_ATTACH));
        } else {
            ::GenericRxEbpfAttach();
        }
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfDrop) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_DROP));
        } else {
            ::GenericRxEbpfDrop();
        }
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfPass) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_PASS));
        } else {
            ::GenericRxEbpfPass();
        }
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfTx) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_TX));
        } else {
            ::GenericRxEbpfTx();
        }
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfPayload) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_PAYLOAD));
        } else {
            ::GenericRxEbpfPayload();
        }
    }

    TEST_METHOD_PRERELEASE(ProgTestRunRxEbpfPayload) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_PROG_TEST_RUN_RX_EBPF_PAYLOAD));
        } else {
            ::ProgTestRunRxEbpfPayload();
        }
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfIfIndex) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_IF_INDEX));
        } else {
            ::GenericRxEbpfIfIndex();
        }
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfFragments) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_FRAGMENTS));
        } else {
            ::GenericRxEbpfFragments();
        }
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfUnload) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_RX_EBPF_UNLOAD));
        } else {
            ::GenericRxEbpfUnload();
        }
    }

    TEST_METHOD(GenericLoopbackV4) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_LOOPBACK_V4, AF_INET));
        } else {
            GenericLoopback(AF_INET);
        }
    }

    TEST_METHOD(GenericLoopbackV6) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_LOOPBACK_V6, AF_INET6));
        } else {
            GenericLoopback(AF_INET6);
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssError) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_ERROR));
        } else {
            ::OffloadRssError();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssReference) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_REFERENCE));
        } else {
            ::OffloadRssReference();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssInterfaceRestart) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_INTERFACE_RESTART));
        } else {
            ::OffloadRssInterfaceRestart();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssUnchanged) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_UNCHANGED));
        } else {
            ::OffloadRssUnchanged();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssUpperSet) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_UPPER_SET));
        } else {
            ::OffloadRssUpperSet();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssSet) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_SET));
        } else {
            ::OffloadRssSet();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssCapabilities) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_CAPABILITIES));
        } else {
            ::OffloadRssCapabilities();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadRssReset) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_RSS_RESET));
        } else {
            ::OffloadRssReset();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadSetHardwareCapabilities) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_SET_HARDWARE_CAPABILITIES));
        } else {
            ::OffloadSetHardwareCapabilities();
        }
    }

    TEST_METHOD(GenericXskQueryAffinity) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_GENERIC_XSK_QUERY_AFFINITY));
        } else {
            ::GenericXskQueryAffinity();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadQeoConnection) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_QEO_CONNECTION));
        } else {
            ::OffloadQeoConnection();
        }
    }

    TEST_METHOD_PRERELEASE(OffloadQeoRevertInterfaceRemoval) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_QEO_REVERT_INTERFACE_REMOVAL, RevertReasonInterfaceRemoval));
        } else {
            ::OffloadQeoRevert(RevertReasonInterfaceRemoval);
        }
    }

    TEST_METHOD_PRERELEASE(OffloadQeoRevertHandleClosure) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_QEO_REVERT_HANDLE_CLOSURE, RevertReasonHandleClosure));
        } else {
            ::OffloadQeoRevert(RevertReasonHandleClosure);
        }
    }

    TEST_METHOD_PRERELEASE(OffloadQeoOidFailure) {
        if (TestingKernelMode) {
            TEST_TRUE(TestDriverClient.Run(IOCTL_OFFLOAD_QEO_OID_FAILURE));
        } else {
            ::OffloadQeoOidFailure();
        }
    }

    TEST_METHOD(OidPassthru) {
        ::OidPassthru();
    }
};
