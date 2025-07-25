//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma warning(disable:26495)  // Always initialize a variable
#pragma warning(disable:26812)  // The enum type '_XDP_MODE' is unscoped.

// Windows and WIL includes need to be ordered in a certain way.
#define NOMINMAX
#include <xdp/wincommon.h>
#include <winsock2.h>
#pragma warning(push)
#pragma warning(disable:4324) // structure was padded due to alignment specifier
#include <ntddndis.h>
#pragma warning(pop)
#include <netiodef.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <lm.h>
#include <sddl.h>
#include <string.h>

#pragma warning(push)
#pragma warning(disable:26457) // (void) should not be used to ignore return values, use 'std::ignore =' instead (es.48)
#include <wil/resource.h>
#pragma warning(pop)

#include <afxdp.h>
#include <afxdp_experimental.h>
#include <afxdp_helper.h>
#include <xdpapi.h>
#include <xdpapi_experimental.h>
#include <pkthlp.h>
#include <fnmpapi.h>
#include <fnlwfapi.h>
#include <fnoid.h>
#include <xdpndisuser.h>
#include <fntrace.h>
#include <qeo_ndis.h>
#include <xdp/ebpfhook.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cxplat.h"
#include "cxplatvector.h"
#include "fnsock.h"
#include "xdptest.h"
#include "tests.h"
#include "util.h"

#include "cxplat.hpp"

#include "tests.tmh"

#define FNMP_IF_DESC "FNMP"
#define FNMP_IPV4_ADDRESS "192.168.200.1"
#define FNMP_IPV6_ADDRESS "fc00::200:1"

#define FNMP1Q_IF_DESC "FNMP #2"
#define FNMP1Q_IPV4_ADDRESS "192.168.201.1"
#define FNMP1Q_IPV6_ADDRESS "fc00::201:1"

#define DEFAULT_UMEM_SIZE 65536
#define DEFAULT_UMEM_CHUNK_SIZE 4096
#define DEFAULT_UMEM_HEADROOM 0
#define DEFAULT_RING_SIZE (DEFAULT_UMEM_SIZE / DEFAULT_UMEM_CHUNK_SIZE)

#define DEFAULT_XDP_SDDL "D:P(A;;GA;;;SY)(A;;GA;;;BA)"

static const XDP_HOOK_ID XdpInspectRxL2 =
{
    XDP_HOOK_L2,
    XDP_HOOK_RX,
    XDP_HOOK_INSPECT,
};

static const XDP_HOOK_ID XdpInspectTxL2 =
{
    XDP_HOOK_L2,
    XDP_HOOK_TX,
    XDP_HOOK_INSPECT,
};

//
// A timeout value that allows for a little latency, e.g. async threads to
// execute.
//
#define TEST_TIMEOUT_ASYNC_MS 1000

//
// The expected maximum time needed for a network adapter to restart.
//
#define MP_RESTART_TIMEOUT_MS 15000

//
// Interval between polling attempts.
//
#define POLL_INTERVAL_MS 10
C_ASSERT(POLL_INTERVAL_MS * 5 <= TEST_TIMEOUT_ASYNC_MS);
C_ASSERT(POLL_INTERVAL_MS * 5 <= MP_RESTART_TIMEOUT_MS);

class TestInterface;

static
VOID
MpTxFilterReset(
    _In_ const FNMP_HANDLE Handle
    )
{
    TEST_HRESULT(FnMpTxFilter(Handle, NULL, NULL, 0));
}

static
VOID
LwfRxFilterReset(
    _In_ const FNLWF_HANDLE Handle
    )
{
    TEST_HRESULT(FnLwfRxFilter(Handle, NULL, NULL, 0));
}

static
VOID
MpTaskOffloadReset(
    _In_ const FNMP_HANDLE Handle
    );

static
void*
AllocMem(
    _In_ SIZE_T Size
    )
{
    void* Mem = CXPLAT_ALLOC_NONPAGED(Size, 'tPDX');
    if (Mem != NULL) {
        CxPlatZeroMemory(Mem, Size);
    }
    return Mem;
}

static
void
FreeMem(
    _In_ void *Mem
    )
{
    CXPLAT_FREE(Mem, 'tPDX');
}

template <typename T>
using unique_malloc_ptr = wistd::unique_ptr<T, wil::function_deleter<decltype(&::FreeMem), ::FreeMem>>;
using unique_bpf_object = wistd::unique_ptr<bpf_object, wil::function_deleter<decltype(&::bpf_object__close), ::bpf_object__close>>;
using unique_fnmp_handle = wil::unique_any<FNMP_HANDLE, decltype(::FnMpClose), ::FnMpClose>;
using unique_fnlwf_handle = wil::unique_any<FNLWF_HANDLE, decltype(::FnLwfClose), ::FnLwfClose>;
using unique_fnmp_filter_handle = wil::unique_any<FNMP_HANDLE, decltype(::MpTxFilterReset), ::MpTxFilterReset>;
using unique_fnlwf_filter_handle = wil::unique_any<FNLWF_HANDLE, decltype(::LwfRxFilterReset), ::LwfRxFilterReset>;
using unique_fnsock = wil::unique_any<FNSOCK_HANDLE, decltype(::FnSockClose), ::FnSockClose>;
using unique_fnmp_task_offload_handle = wil::unique_any<FNMP_HANDLE, decltype(::MpTaskOffloadReset), ::MpTaskOffloadReset>;

static
VOID
MpResetMtu(
    _In_ const TestInterface *If
    );

using unique_fnmp_mtu_handle = wil::unique_any<const TestInterface *, decltype(::MpResetMtu), ::MpResetMtu>;

static FNMP_LOAD_API_CONTEXT FnMpLoadApiContext;
static FNLWF_LOAD_API_CONTEXT FnLwfLoadApiContext;

typedef enum _XDP_MODE {
    XDP_UNSPEC,
    XDP_GENERIC,
    XDP_NATIVE,
} XDP_MODE;

typedef struct {
    XSK_RING Fill;
    XSK_RING Completion;
    XSK_RING Rx;
    XSK_RING Tx;
} RING_SET;

typedef struct {
    XSK_UMEM_REG Reg;
    wil::unique_virtualalloc_ptr<UCHAR> Buffer;
} MY_UMEM;

typedef struct {
    BOOLEAN TxFrameLayoutExtensionEnabled;
    UINT16 TxFrameLayoutExtension;
    BOOLEAN TxFrameChecksumExtensionEnabled;
    UINT16 TxFrameChecksumExtension;
    BOOLEAN RxFrameLayoutExtensionEnabled;
    UINT16 RxFrameLayoutExtension;
    BOOLEAN RxFrameChecksumExtensionEnabled;
    UINT16 RxFrameChecksumExtension;
} MY_EXTENSIONS;

typedef struct {
    UINT32 QueueId;
    MY_UMEM Umem;
    wil::unique_handle Handle;
    wil::unique_handle RxProgram;
    RING_SET Rings;
    CxPlatVector<UINT64> FreeDescriptors;
    MY_EXTENSIONS Extensions;
} MY_SOCKET;

typedef struct {
    BOOLEAN Rx;
    BOOLEAN Tx;
} RX_TX_TESTCASE;

static RX_TX_TESTCASE RxTxTestCases[] = {
    { TRUE, FALSE },
    { FALSE, TRUE },
    { TRUE, TRUE }
};

static const CHAR *PowershellPrefix;
static RTL_OSVERSIONINFOW OsVersionInfo;

//
// Helper functions.
//

static
VOID
WaitForNdisDatapath(
    _In_ const TestInterface &If,
    _In_ UINT64 TimeoutInMs = TEST_TIMEOUT_ASYNC_MS
    );

static
BOOLEAN
TryWaitForNdisDatapath(
    _In_ const TestInterface &If,
    _In_ UINT64 TimeoutInMs = TEST_TIMEOUT_ASYNC_MS
    );

static
INT
InvokeSystem(
    _In_z_ const CHAR *Command
    )
{
    INT Result;

    TraceVerbose("system(%s)", Command);
    Result = system(Command);
    TraceVerbose("system(%s) returned %u", Command, Result);

    return Result;
}

typedef NTSTATUS (WINAPI* RTL_GET_VERSION_FN)(PRTL_OSVERSIONINFOW);

VOID
GetOSVersion(
    )
{
    HMODULE Module = GetModuleHandleW(L"ntdll.dll");
    TEST_TRUE(Module != NULL);

    RTL_GET_VERSION_FN RtlGetVersion = (RTL_GET_VERSION_FN)GetProcAddress(Module, "RtlGetVersion");
    TEST_TRUE(RtlGetVersion != NULL);

    OsVersionInfo.dwOSVersionInfoSize = sizeof(OsVersionInfo);
    TEST_EQUAL(STATUS_SUCCESS, RtlGetVersion(&OsVersionInfo));
}

BOOLEAN
OsVersionIsVbOrLater()
{
    return
        (OsVersionInfo.dwMajorVersion > 10 || (OsVersionInfo.dwMajorVersion == 10 &&
        (OsVersionInfo.dwMinorVersion > 0 || (OsVersionInfo.dwMinorVersion == 0 &&
        (OsVersionInfo.dwBuildNumber >= 19041)))));
}

BOOLEAN
OsVersionIsFeOrLater()
{
    return
        (OsVersionInfo.dwMajorVersion > 10 || (OsVersionInfo.dwMajorVersion == 10 &&
        (OsVersionInfo.dwMinorVersion > 0 || (OsVersionInfo.dwMinorVersion == 0 &&
        (OsVersionInfo.dwBuildNumber >= 20100)))));
}

BOOLEAN
OsVersionIsGaOrLater()
{
    //
    // N.B. The version number here is an estimate. The actual version number
    // should be set once [Ga] releases.
    //
    return
        (OsVersionInfo.dwMajorVersion > 10 || (OsVersionInfo.dwMajorVersion == 10 &&
        (OsVersionInfo.dwMinorVersion > 0 || (OsVersionInfo.dwMinorVersion == 0 &&
        (OsVersionInfo.dwBuildNumber >= 25892)))));
}

UINT32
GetProcessorCount(
    _In_opt_ UINT16 Group = ALL_PROCESSOR_GROUPS
    )
{
    UINT32 Count = GetActiveProcessorCount(Group);
    TEST_NOT_EQUAL(0, Count);
    return Count;
}

VOID
ProcessorIndexToProcessorNumber(
    _In_ UINT32 ProcIndex,
    _Out_ PROCESSOR_NUMBER *ProcNumber
    )
{
    UINT16 Group = 0;

    //
    // Convert a convenient group-agnostic processor index (not necessarily
    // matching the kernel/NT processor index) into the group-relative processor
    // number.
    //

    RtlZeroMemory(ProcNumber, sizeof(*ProcNumber));

    while (GetProcessorCount(Group) < ProcIndex) {
        ProcIndex -= GetProcessorCount(Group);
        Group++;
    }

    ProcNumber->Group = Group;
    ProcNumber->Number = (UINT8)ProcIndex;
}

static
VOID
SetBit(
    _Inout_ UINT8 *BitMap,
    _In_ UINT32 Index
    )
{
    BitMap[Index >> 3] |= (1 << (Index & 0x7));
}

static
VOID
ClearBit(
    _Inout_ UINT8 *BitMap,
    _In_ UINT32 Index
    )
{
    BitMap[Index >> 3] &= (UINT8)~(1 << (Index & 0x7));
}

static
UINT32
GetRandomUInt32()
{
    UINT32 Random;
    CxPlatRandom(sizeof(Random), &Random);
    return Random;
}

class Stopwatch {
private:
    UINT64 _StartQpc;
    UINT64 _TimeoutInterval;

public:
    Stopwatch(
        _In_opt_ UINT64 TimeoutInterval = MAXUINT64
        )
        :
        _TimeoutInterval(TimeoutInterval)
    {
        _StartQpc = CxPlatTimePlat();
    }

    UINT64
    Elapsed()
    {
        UINT64 End;
        UINT64 ElapsedQpc;

        End = CxPlatTimePlat();
        ElapsedQpc = End - _StartQpc;

        return US_TO_MS(CxPlatTimePlatToUs64(ElapsedQpc));
    }

    UINT64
    Remaining()
    {
        UINT64 Remaining = _TimeoutInterval - Elapsed();

        if (Remaining > _TimeoutInterval) {
            return 0;
        } else {
            return Remaining;
        }
    }

    bool
    IsExpired()
    {
        return Elapsed() >= _TimeoutInterval;
    }

    void
    ExpectElapsed(
        _In_ UINT64 ExpectedInterval,
        _In_opt_ UINT32 MarginPercent = 10
        )
    {
        UINT64 Fudge = (ExpectedInterval * MarginPercent) / 100;
        TEST_TRUE(MarginPercent == 0 || Fudge > 0);
        TEST_TRUE(Elapsed() >= ExpectedInterval - Fudge);
        TEST_TRUE(Elapsed() <= ExpectedInterval + Fudge);
    }

    void
    Reset()
    {
        _StartQpc = CxPlatTimePlat();
    }

    void
    Reset(
        _In_ UINT64 TimeoutInterval
        )
    {
        _TimeoutInterval = TimeoutInterval;
        Reset();
    }
};

class TestInterface {
private:
    const CHAR *_IfDesc;
    mutable UINT32 _IfIndex;
    mutable UCHAR _HwAddress[sizeof(ETHERNET_ADDRESS)]{ 0 };
    IN_ADDR _Ipv4Address;
    IN6_ADDR _Ipv6Address;

    VOID
    Query() const
    {
        IP_ADAPTER_INFO *Adapter;
        ULONG OutBufLen;

        if (ReadUInt32Acquire(&_IfIndex) != NET_IFINDEX_UNSPECIFIED) {
            return;
        }

        //
        // Get information on all adapters.
        //
        OutBufLen = 0;
        TEST_EQUAL(ERROR_BUFFER_OVERFLOW, GetAdaptersInfo(NULL, &OutBufLen));
        unique_malloc_ptr<IP_ADAPTER_INFO> AdapterInfoList{ (IP_ADAPTER_INFO *)AllocMem(OutBufLen) };
        TEST_NOT_NULL(AdapterInfoList.get());
        TEST_EQUAL(NO_ERROR, GetAdaptersInfo(AdapterInfoList.get(), &OutBufLen));

        //
        // Search for the test adapter.
        //
        Adapter = AdapterInfoList.get();
        while (Adapter != NULL) {
            if (!strcmp(Adapter->Description, _IfDesc)) {
                TEST_EQUAL(sizeof(_HwAddress), Adapter->AddressLength);
                RtlCopyMemory(_HwAddress, Adapter->Address, sizeof(_HwAddress));

                WriteUInt32Release(&_IfIndex, Adapter->Index);
            }
            Adapter = Adapter->Next;
        }

        TEST_NOT_EQUAL(NET_IFINDEX_UNSPECIFIED, _IfIndex);
    }

public:

    TestInterface(
        _In_z_ const CHAR *IfDesc,
        _In_z_ const CHAR *Ipv4Address,
        _In_z_ const CHAR *Ipv6Address
        )
        :
        _IfDesc(IfDesc),
        _IfIndex(NET_IFINDEX_UNSPECIFIED)
    {
        const CHAR *Terminator;
        TEST_NTSTATUS(RtlIpv4StringToAddressA(Ipv4Address, FALSE, &Terminator, &_Ipv4Address));
        TEST_NTSTATUS(RtlIpv6StringToAddressA(Ipv6Address, &Terminator, &_Ipv6Address));
    }

    const CHAR*
    GetIfDesc() const
    {
        return _IfDesc;
    }

    NET_IFINDEX
    GetIfIndex() const
    {
        Query();
        return _IfIndex;
    }

    UINT32
    GetQueueId() const
    {
        return 0;
    }

    VOID
    GetHwAddress(
        _Out_ ETHERNET_ADDRESS *HwAddress
        ) const
    {
        Query();
        RtlCopyMemory(HwAddress, _HwAddress, sizeof(_HwAddress));
    }

    VOID
    GetRemoteHwAddress(
        _Out_ ETHERNET_ADDRESS *HwAddress
        ) const
    {
        GetHwAddress(HwAddress);
        HwAddress->Byte[sizeof(_HwAddress) - 1]++;
    }

    VOID
    GetIpv4Address(
        _Out_ IN_ADDR *Ipv4Address
        ) const
    {
        *Ipv4Address = _Ipv4Address;
    }

    VOID
    GetRemoteIpv4Address(
        _Out_ IN_ADDR *Ipv4Address
        ) const
    {
        GetIpv4Address(Ipv4Address);
        Ipv4Address->S_un.S_un_b.s_b4++;
    }

    VOID
    GetIpv6Address(
        _Out_ IN6_ADDR *Ipv6Address
        ) const
    {
        *Ipv6Address = _Ipv6Address;
    }

    VOID
    GetRemoteIpv6Address(
        _Out_ IN6_ADDR *Ipv6Address
        ) const
    {
        GetIpv6Address(Ipv6Address);
        Ipv6Address->u.Byte[sizeof(*Ipv6Address) - 1]++;
    }

    BOOLEAN
    TryRestart(BOOLEAN WaitForUp = TRUE) const
    {
        CHAR CmdBuff[256];
        INT ExitCode;
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(CmdBuff, "%s /c Restart-NetAdapter -ifDesc \"%s\"", PowershellPrefix, _IfDesc);
        ExitCode = InvokeSystem(CmdBuff);

        if (ExitCode != 0) {
            TraceError("ExitCode=%u", ExitCode);
            return FALSE;
        }

        if (WaitForUp && !TryWaitForNdisDatapath(*this)) {
            TraceError("TryWaitForNdisDatapath=FALSE");
            return FALSE;
        }

        return TRUE;
    }

    VOID
    Restart(BOOLEAN WaitForUp = TRUE) const
    {
        TEST_TRUE(TryRestart(WaitForUp));
    }

    VOID
    Reset() const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(CmdBuff, "%s /c Reset-NetAdapterAdvancedProperty -ifDesc \"%s\" -DisplayName * -NoRestart", PowershellPrefix, _IfDesc);
        TEST_EQUAL(0, InvokeSystem(CmdBuff));
        Restart();
    }

    HRESULT
    TryUnbindXdp() const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(
            CmdBuff,
            "%s /c \"(Get-NetAdapter -ifDesc '%s') | Disable-NetAdapterBinding -ComponentID ms_xdp",
            PowershellPrefix, _IfDesc);
        return HRESULT_FROM_WIN32(InvokeSystem(CmdBuff));
    }

    HRESULT
    TryRebindXdp() const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(
            CmdBuff,
            "%s /c \"(Get-NetAdapter -ifDesc '%s') | Enable-NetAdapterBinding -ComponentID ms_xdp",
            PowershellPrefix, _IfDesc);
        return HRESULT_FROM_WIN32(InvokeSystem(CmdBuff));
    }
};

static TestInterface FnMpIf(FNMP_IF_DESC, FNMP_IPV4_ADDRESS, FNMP_IPV6_ADDRESS);
static TestInterface FnMp1QIf(FNMP1Q_IF_DESC, FNMP1Q_IPV4_ADDRESS, FNMP1Q_IPV6_ADDRESS);

static
VOID
WaitForWfpQuarantine(
    _In_ const TestInterface &If
    );

static
HRESULT
TryStartService(
    _In_z_ const CHAR *ServiceName
    )
{
    HRESULT Result;
    UINT32 ServiceState;

    TraceVerbose("Starting %s", ServiceName);

    Result = StartServiceAsync(ServiceName);
    if (FAILED(Result)) {
        TraceError("StartServiceAsync failed Result=%!HRESULT!", Result);
        return Result;
    }

    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    do {
        Result = GetServiceState(&ServiceState, ServiceName);
        if (FAILED(Result)) {
            TraceError("GetServiceState failed Result=%!HRESULT!", Result);
            return Result;
        }
        if (ServiceState == SERVICE_RUNNING) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    Result = (ServiceState == SERVICE_RUNNING) ? S_OK : E_FAIL;
    TraceVerbose("ServiceState=%u Result=%!HRESULT!", ServiceState, Result);
    return Result;
}

static
HRESULT
TryStopService(
    _In_z_ const CHAR *ServiceName
    )
{
    HRESULT Result;
    UINT32 ServiceState;

    TraceVerbose("Stopping %s", ServiceName);

    Result = StopServiceAsync(ServiceName);
    if (FAILED(Result)) {
        TraceError("StopServiceAsync failed Result=%!HRESULT!", Result);
        return Result;
    }

    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    do {
        Result = GetServiceState(&ServiceState, ServiceName);
        if (FAILED(Result)) {
            TraceError("GetServiceState failed Result=%!HRESULT!", Result);
            return Result;
        }
        if (ServiceState == SERVICE_STOPPED) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    Result = (ServiceState == SERVICE_STOPPED) ? S_OK : E_FAIL;
    TraceVerbose("ServiceState=%u Result=%!HRESULT!", ServiceState, Result);
    return Result;
}

static
HRESULT
TryRestartService(
    _In_z_ const CHAR *ServiceName
    )
{
    HRESULT Result;

    Result = TryStopService(ServiceName);
    if (FAILED(Result)) {
        return Result;
    }

    return TryStartService(ServiceName);
}

static
VOID
SetDeviceSddl(
    _In_z_ const CHAR *Sddl
    )
{
    CHAR XdpBinaryPath[MAX_PATH];
    UINT32 XdpBinaryPathLength;
    CHAR CmdBuff[256];

    XdpBinaryPathLength =
        GetEnvironmentVariableA("_XDP_BINARIES_PATH", XdpBinaryPath, sizeof(XdpBinaryPath));
    TEST_NOT_EQUAL(0, XdpBinaryPathLength);
    TEST_TRUE(XdpBinaryPathLength <= sizeof(XdpBinaryPath));

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(CmdBuff, "%s\\xdpcfg.exe SetDeviceSddl \"%s\"", XdpBinaryPath, Sddl);
    TEST_EQUAL(0, InvokeSystem(CmdBuff));
}

static
HRESULT
TryCreateSocket(
    _Inout_ wil::unique_handle &Socket
    )
{
    return XskCreate(&Socket);
}

static
wil::unique_handle
CreateSocket()
{
    wil::unique_handle Socket;
    TEST_HRESULT(TryCreateSocket(Socket));
    return Socket;
}

static
wil::unique_virtualalloc_ptr<UCHAR>
AllocUmemBuffer()
{
    wil::unique_virtualalloc_ptr<UCHAR> Buffer(
        (UCHAR *)VirtualAlloc(
            NULL,
            DEFAULT_UMEM_SIZE,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    TEST_NOT_NULL(Buffer.get());
    return Buffer;
}

static
VOID
InitUmem(
    _Out_ XSK_UMEM_REG *UmemRegistration,
    VOID *UmemBuffer
    )
{
    UmemRegistration->TotalSize = DEFAULT_UMEM_SIZE;
    UmemRegistration->ChunkSize = DEFAULT_UMEM_CHUNK_SIZE;
    UmemRegistration->Headroom = DEFAULT_UMEM_HEADROOM;
    UmemRegistration->Address = UmemBuffer;
}

static
HRESULT
TryGetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    )
{
    return XskGetSockopt(Socket, OptionName, OptionValue, OptionLength);
}

static
VOID
GetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _Out_writes_bytes_(*OptionLength) VOID *OptionValue,
    _Inout_ UINT32 *OptionLength
    )
{
    TEST_HRESULT(TryGetSockopt(Socket, OptionName, OptionValue, OptionLength));
}

static
HRESULT
TrySetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    )
{
    return XskSetSockopt(Socket, OptionName, OptionValue, OptionLength);
}

static
VOID
SetSockopt(
    _In_ HANDLE Socket,
    _In_ UINT32 OptionName,
    _In_reads_bytes_opt_(OptionLength) const VOID *OptionValue,
    _In_ UINT32 OptionLength
    )
{
    TEST_HRESULT(TrySetSockopt(Socket, OptionName, OptionValue, OptionLength));
}

static
VOID
SetUmem(
    _In_ HANDLE Socket,
    _In_ XSK_UMEM_REG *UmemRegistration
    )
{
    SetSockopt(Socket, XSK_SOCKOPT_UMEM_REG, UmemRegistration, sizeof(*UmemRegistration));
}

static
VOID
GetRingInfo(
    _In_ HANDLE Socket,
    _Out_ XSK_RING_INFO_SET *InfoSet
    )
{
    UINT32 InfoSize = sizeof(*InfoSet);
    GetSockopt(Socket, XSK_SOCKOPT_RING_INFO, InfoSet, &InfoSize);
    TEST_EQUAL(sizeof(*InfoSet), InfoSize);
}

static
VOID
SetFillRing(
    _In_ HANDLE Socket,
    _In_opt_ UINT32 RingSize = DEFAULT_RING_SIZE
    )
{
    XSK_RING_INFO_SET InfoSet;

    SetSockopt(Socket, XSK_SOCKOPT_RX_FILL_RING_SIZE, &RingSize, sizeof(RingSize));
    GetRingInfo(Socket, &InfoSet);
    TEST_EQUAL(RingSize, InfoSet.Fill.Size);
}

static
VOID
SetCompletionRing(
    _In_ HANDLE Socket,
    _In_opt_ UINT32 RingSize = DEFAULT_RING_SIZE
    )
{
    XSK_RING_INFO_SET InfoSet;

    SetSockopt(Socket, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &RingSize, sizeof(RingSize));
    GetRingInfo(Socket, &InfoSet);
    TEST_EQUAL(RingSize, InfoSet.Completion.Size);
}

static
VOID
SetRxRing(
    _In_ HANDLE Socket,
    _In_opt_ UINT32 RingSize = DEFAULT_RING_SIZE
    )
{
    XSK_RING_INFO_SET InfoSet;

    SetSockopt(Socket, XSK_SOCKOPT_RX_RING_SIZE, &RingSize, sizeof(RingSize));
    GetRingInfo(Socket, &InfoSet);
    TEST_EQUAL(RingSize, InfoSet.Rx.Size);
}

static
VOID
SetTxRing(
    _In_ HANDLE Socket,
    _In_opt_ UINT32 RingSize = DEFAULT_RING_SIZE
    )
{
    XSK_RING_INFO_SET InfoSet;

    SetSockopt(Socket, XSK_SOCKOPT_TX_RING_SIZE, &RingSize, sizeof(RingSize));
    GetRingInfo(Socket, &InfoSet);
    TEST_EQUAL(RingSize, InfoSet.Tx.Size);
}

static
VOID
SetRxHookId(
    _In_ HANDLE Socket,
    _In_ const XDP_HOOK_ID *HookId
    )
{
    SetSockopt(Socket, XSK_SOCKOPT_RX_HOOK_ID, HookId, sizeof(*HookId));
}

static
VOID
SetTxHookId(
    _In_ HANDLE Socket,
    _In_ const XDP_HOOK_ID *HookId
    )
{
    SetSockopt(Socket, XSK_SOCKOPT_TX_HOOK_ID, HookId, sizeof(*HookId));
}

static
HRESULT
TryNotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    return XskNotifySocket(Socket, Flags, WaitTimeoutMilliseconds, Result);
}

static
VOID
NotifySocket(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _In_ UINT32 WaitTimeoutMilliseconds,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    TEST_HRESULT(TryNotifySocket(Socket, Flags, WaitTimeoutMilliseconds, Result));
}

static
HRESULT
TryNotifyAsync(
    _In_ HANDLE Socket,
    _In_ XSK_NOTIFY_FLAGS Flags,
    _Inout_ OVERLAPPED *Overlapped
    )
{
    return XskNotifyAsync(Socket, Flags, Overlapped);
}

static
HRESULT
TryGetNotifyAsyncResult(
    _In_ OVERLAPPED *Overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    return XskGetNotifyAsyncResult(Overlapped, Result);
}

static
VOID
GetNotifyAsyncResult(
    _In_ OVERLAPPED *Overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    TEST_HRESULT(TryGetNotifyAsyncResult(Overlapped, Result));
}

static
HRESULT
TryInterfaceOpen(
    _In_ UINT32 InterfaceIndex,
    _Out_ wil::unique_handle &InterfaceHandle
    )
{
    return XdpInterfaceOpen(InterfaceIndex, &InterfaceHandle);
}

static
wil::unique_handle
InterfaceOpen(
    _In_ UINT32 InterfaceIndex
    )
{
    wil::unique_handle InterfaceHandle;
    TEST_HRESULT(TryInterfaceOpen(InterfaceIndex, InterfaceHandle));
    return InterfaceHandle;
}

static
HRESULT
TryRssGetCapabilities(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    )
{
    return XdpRssGetCapabilities(InterfaceHandle, RssCapabilities, RssCapabilitiesSize);
}

static
VOID
RssGetCapabilities(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CAPABILITIES *RssCapabilities,
    _Inout_ UINT32 *RssCapabilitiesSize
    )
{
    TEST_HRESULT(TryRssGetCapabilities(InterfaceHandle, RssCapabilities, RssCapabilitiesSize));
}

static
HRESULT
TryRssSet(
    _In_ HANDLE InterfaceHandle,
    _In_ const XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    return XdpRssSet(InterfaceHandle, RssConfiguration, RssConfigurationSize);
}

static
VOID
RssSet(
    _In_ HANDLE InterfaceHandle,
    _In_ const XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    TEST_HRESULT(TryRssSet(InterfaceHandle, RssConfiguration, RssConfigurationSize));
}

static
HRESULT
TryRssGet(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    )
{
    return XdpRssGet(InterfaceHandle, RssConfiguration, RssConfigurationSize);
}

static
VOID
RssGet(
    _In_ HANDLE InterfaceHandle,
    _Out_opt_ XDP_RSS_CONFIGURATION *RssConfiguration,
    _Inout_ UINT32 *RssConfigurationSize
    )
{
    TEST_HRESULT(TryRssGet(InterfaceHandle, RssConfiguration, RssConfigurationSize));
}

static
HRESULT
TryQeoSet(
    _In_ HANDLE InterfaceHandle,
    _In_ XDP_QUIC_CONNECTION *QuicConnections,
    _In_ UINT32 QuicConnectionsSize
    )
{
    return XdpQeoSet(InterfaceHandle, QuicConnections, QuicConnectionsSize);
}

static
HRESULT
TryCreateXdpProg(
    _Out_ wil::unique_handle &ProgramHandle,
    _In_ UINT32 IfIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_MODE XdpMode,
    _In_ XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags = XDP_CREATE_PROGRAM_FLAG_NONE
    )
{
    ASSERT(Flags & (XDP_CREATE_PROGRAM_FLAG_GENERIC | XDP_CREATE_PROGRAM_FLAG_NATIVE) == 0);

    if (XdpMode == XDP_GENERIC) {
        Flags |= XDP_CREATE_PROGRAM_FLAG_GENERIC;
    } else if (XdpMode == XDP_NATIVE) {
        Flags |= XDP_CREATE_PROGRAM_FLAG_NATIVE;
    }

    return
        XdpCreateProgram(IfIndex, HookId, QueueId, Flags, Rules, RuleCount, &ProgramHandle);
}

static
wil::unique_handle
CreateXdpProg(
    _In_ UINT32 IfIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_MODE XdpMode,
    _In_ XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _In_ XDP_CREATE_PROGRAM_FLAGS Flags = XDP_CREATE_PROGRAM_FLAG_NONE
    )
{
    wil::unique_handle ProgramHandle;

    TEST_HRESULT(
        TryCreateXdpProg(
            ProgramHandle, IfIndex, HookId, QueueId, XdpMode, Rules, RuleCount, Flags));

    return ProgramHandle;
}

static
wil::unique_handle
SocketAttachRxProgram(
    _In_ UINT32 IfIndex,
    _In_ const XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_MODE XdpMode,
    _In_ HANDLE Socket
    )
{
    XDP_RULE Rule = {};

    Rule.Match = XDP_MATCH_ALL;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Socket;

    return CreateXdpProg(IfIndex, HookId, QueueId, XdpMode, &Rule, 1);
}

static
VOID
XskSetupPreBind(
    _Inout_ MY_SOCKET *Socket,
    _In_opt_ const XDP_HOOK_ID *RxHookId = nullptr,
    _In_opt_ const XDP_HOOK_ID *TxHookId = nullptr
    )
{
    Socket->Umem.Buffer = AllocUmemBuffer();
    InitUmem(&Socket->Umem.Reg, Socket->Umem.Buffer.get());
    SetUmem(Socket->Handle.get(), &Socket->Umem.Reg);

    if (RxHookId != nullptr) {
        SetRxHookId(Socket->Handle.get(), RxHookId);
    }
    if (TxHookId != nullptr) {
        SetTxHookId(Socket->Handle.get(), TxHookId);
    }
}

static
MY_SOCKET
CreateAndBindSocket(
    NET_IFINDEX IfIndex,
    UINT32 QueueId,
    BOOLEAN Rx,
    BOOLEAN Tx,
    XDP_MODE XdpMode,
    XSK_BIND_FLAGS BindFlags = XSK_BIND_FLAG_NONE,
    const XDP_HOOK_ID *RxHookId = nullptr,
    const XDP_HOOK_ID *TxHookId = nullptr
    )
{
    MY_SOCKET Socket = {0};

    Socket.Handle = CreateSocket();

    XskSetupPreBind(&Socket, RxHookId, TxHookId);

    if (Rx) {
        BindFlags |= XSK_BIND_FLAG_RX;
    }

    if (Tx) {
        BindFlags |= XSK_BIND_FLAG_TX;
    }

    if (XdpMode == XDP_GENERIC) {
        BindFlags |= XSK_BIND_FLAG_GENERIC;
    } else if (XdpMode == XDP_NATIVE) {
        BindFlags |= XSK_BIND_FLAG_NATIVE;
    }

    //
    // Since test actions (e.g. restart) might disrupt the interface, retry
    // bindings until the system must have quiesced.
    //
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    HRESULT BindResult;
    do {
        BindResult = XskBind(Socket.Handle.get(), IfIndex, QueueId, BindFlags);
        if (SUCCEEDED(BindResult)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_HRESULT(BindResult);

    return Socket;
}

static
VOID
EnableTxChecksumOffload(
    MY_SOCKET *Socket
    )
{
    UINT32 Enabled = TRUE;
    SetSockopt(Socket->Handle.get(), XSK_SOCKOPT_TX_OFFLOAD_CHECKSUM, &Enabled, sizeof(Enabled));
    Socket->Extensions.TxFrameLayoutExtensionEnabled = TRUE;
    Socket->Extensions.TxFrameChecksumExtensionEnabled = TRUE;
}

static
VOID
EnableRxChecksumOffload(
    MY_SOCKET *Socket
    )
{
    UINT32 Enabled = TRUE;
    SetSockopt(Socket->Handle.get(), XSK_SOCKOPT_RX_OFFLOAD_CHECKSUM, &Enabled, sizeof(Enabled));
    Socket->Extensions.RxFrameLayoutExtensionEnabled = TRUE;
    Socket->Extensions.RxFrameChecksumExtensionEnabled = TRUE;
}

static
VOID
XskSetupPreActivate(
    _Inout_ MY_SOCKET *Socket,
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    )
{
    XSK_RING_INFO_SET InfoSet;
    UINT32 OptionLength;

    SetFillRing(Socket->Handle.get());
    SetCompletionRing(Socket->Handle.get());

    if (Rx) {
        SetRxRing(Socket->Handle.get());
    }

    if (Tx) {
        SetTxRing(Socket->Handle.get());
    }

    GetRingInfo(Socket->Handle.get(), &InfoSet);
    XskRingInitialize(&Socket->Rings.Fill, &InfoSet.Fill);
    XskRingInitialize(&Socket->Rings.Completion, &InfoSet.Completion);

    if (Rx) {
        XskRingInitialize(&Socket->Rings.Rx, &InfoSet.Rx);
    }

    if (Tx) {
        XskRingInitialize(&Socket->Rings.Tx, &InfoSet.Tx);
    }

    UINT64 BufferCount = Socket->Umem.Reg.TotalSize / Socket->Umem.Reg.ChunkSize;
    UINT64 Offset = 0;
    while (BufferCount-- > 0) {
        TEST_TRUE(Socket->FreeDescriptors.push_back(Offset));
        Offset += Socket->Umem.Reg.ChunkSize;
    }

    if (Socket->Extensions.TxFrameLayoutExtensionEnabled) {
        OptionLength = sizeof(Socket->Extensions.TxFrameLayoutExtension);
        GetSockopt(
            Socket->Handle.get(), XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION,
            &Socket->Extensions.TxFrameLayoutExtension, &OptionLength);
    }

    if (Socket->Extensions.TxFrameChecksumExtensionEnabled) {
        OptionLength = sizeof(Socket->Extensions.TxFrameChecksumExtension);
        GetSockopt(
            Socket->Handle.get(), XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION,
            &Socket->Extensions.TxFrameChecksumExtension, &OptionLength);
    }

    if (Socket->Extensions.RxFrameLayoutExtensionEnabled) {
        OptionLength = sizeof(Socket->Extensions.RxFrameLayoutExtension);
        GetSockopt(
            Socket->Handle.get(), XSK_SOCKOPT_RX_FRAME_LAYOUT_EXTENSION,
            &Socket->Extensions.RxFrameLayoutExtension, &OptionLength);
    }

    if (Socket->Extensions.RxFrameChecksumExtensionEnabled) {
        OptionLength = sizeof(Socket->Extensions.RxFrameChecksumExtension);
        GetSockopt(
            Socket->Handle.get(), XSK_SOCKOPT_RX_FRAME_CHECKSUM_EXTENSION,
            &Socket->Extensions.RxFrameChecksumExtension, &OptionLength);
    }
}

static
VOID
ActivateSocket(
    MY_SOCKET *Socket,
    BOOLEAN Rx,
    BOOLEAN Tx
    )
{
    XskSetupPreActivate(Socket, Rx, Tx);

    TEST_HRESULT(XskActivate(Socket->Handle.get(), XSK_ACTIVATE_FLAG_NONE));
}

static
MY_SOCKET
CreateAndActivateSocket(
    NET_IFINDEX IfIndex,
    UINT32 QueueId,
    BOOLEAN Rx,
    BOOLEAN Tx,
    XDP_MODE XdpMode,
    XSK_BIND_FLAGS BindFlags = XSK_BIND_FLAG_NONE,
    const XDP_HOOK_ID *RxHookId = nullptr,
    const XDP_HOOK_ID *TxHookId = nullptr
    )
{
    MY_SOCKET Socket =
        CreateAndBindSocket(IfIndex, QueueId, Rx, Tx, XdpMode, BindFlags, RxHookId, TxHookId);

    ActivateSocket(&Socket, Rx, Tx);

    return Socket;
}

static
MY_SOCKET
SetupSocket(
    NET_IFINDEX IfIndex,
    UINT32 QueueId,
    BOOLEAN Rx,
    BOOLEAN Tx,
    XDP_MODE XdpMode
    )
{
    auto Socket = CreateAndActivateSocket(IfIndex, QueueId, Rx, Tx, XdpMode);

    if (Rx) {
        Socket.RxProgram =
            SocketAttachRxProgram(IfIndex, &XdpInspectRxL2, QueueId, XdpMode, Socket.Handle.get());
    }

    return Socket;
}

static
UINT64
SocketFreePop(
    _In_ MY_SOCKET *Socket
    )
{
    UINT64 Descriptor = Socket->FreeDescriptors[Socket->FreeDescriptors.size() - 1];
    Socket->FreeDescriptors.pop_back();
    return Descriptor;
}

static
UINT64 *
SocketGetRxFillDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    return (UINT64 *)XskRingGetElement(&Socket->Rings.Fill, Index);
}

static
XSK_FRAME_DESCRIPTOR *
SocketGetTxFrameDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    return (XSK_FRAME_DESCRIPTOR *)XskRingGetElement(&Socket->Rings.Tx, Index);
}

static
XSK_FRAME_DESCRIPTOR *
SocketGetRxFrameDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    return (XSK_FRAME_DESCRIPTOR *)XskRingGetElement(&Socket->Rings.Rx, Index);
}

static
XSK_BUFFER_DESCRIPTOR *
SocketGetTxDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    //
    // For legacy/simple applications, the TX ring can be defined simply as a
    // buffer, and the frame descriptor is simply a higher level abstraction.
    //
    C_ASSERT(FIELD_OFFSET(XSK_FRAME_DESCRIPTOR, Buffer) == 0);

    return &SocketGetTxFrameDesc(Socket, Index)->Buffer;
}

static
XSK_BUFFER_DESCRIPTOR *
SocketGetRxDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    //
    // For legacy/simple applications, the RX ring can be defined simply as a
    // buffer, and the frame descriptor is simply a higher level abstraction.
    //
    C_ASSERT(FIELD_OFFSET(XSK_FRAME_DESCRIPTOR, Buffer) == 0);

    return &SocketGetRxFrameDesc(Socket, Index)->Buffer;
}

static
XSK_BUFFER_DESCRIPTOR *
SocketGetAndFreeRxDesc(
    _In_ MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    XSK_BUFFER_DESCRIPTOR * RxDesc = SocketGetRxDesc(Socket, Index);
    TEST_TRUE(Socket->FreeDescriptors.push_back(RxDesc->Address.BaseAddress));
    return RxDesc;
}

static
UINT64
SocketGetTxCompDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    return *(UINT64 *)XskRingGetElement(&Socket->Rings.Completion, Index);
}

static
UINT32
SocketConsumerReserve(
    _In_ XSK_RING *Ring,
    _In_ UINT32 ExpectedCount,
    _In_opt_ UINT64 Timeout = TEST_TIMEOUT_ASYNC_MS
    )
{
    UINT32 Index;
    Stopwatch watchdog(Timeout);
    while (!watchdog.IsExpired()) {
        if (XskRingConsumerReserve(Ring, ExpectedCount, &Index) == ExpectedCount) {
            break;
        }
    }

    TEST_EQUAL(ExpectedCount, XskRingConsumerReserve(Ring, ExpectedCount, &Index));
    return Index;
}

static
VOID
SocketProduceRxFill(
    _In_ MY_SOCKET *Socket,
    _In_ UINT32 Count
    )
{
    UINT32 ProducerIndex;
    TEST_EQUAL(Count, XskRingProducerReserve(&Socket->Rings.Fill, Count, &ProducerIndex));
    while (Count-- > 0) {
        *SocketGetRxFillDesc(Socket, ProducerIndex++) = SocketFreePop(Socket);
    }
    XskRingProducerSubmit(&Socket->Rings.Fill, Count);
}

static
VOID
SocketProducerCheckNeedPoke(
    _In_ XSK_RING *Ring,
    _In_ BOOLEAN ExpectedState,
    _In_opt_ UINT64 Timeout = TEST_TIMEOUT_ASYNC_MS
    )
{
    Stopwatch watchdog(Timeout);
    while (!watchdog.IsExpired()) {
        if (XskRingProducerNeedPoke(Ring) == ExpectedState) {
            break;
        }
    }

    TEST_EQUAL(ExpectedState, XskRingProducerNeedPoke(Ring));
}

static
VOID
InitializeOidKey(
    _Out_ OID_KEY *Key,
    _In_ NDIS_OID Oid,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_opt_ OID_REQUEST_INTERFACE RequestInterface = OID_REQUEST_INTERFACE_REGULAR
    )
{
    RtlZeroMemory(Key, sizeof(*Key));
    Key->Oid = Oid;
    Key->RequestType = RequestType;
    Key->RequestInterface = RequestInterface;
}

static
unique_fnmp_handle
MpOpenGeneric(
    _In_ UINT32 IfIndex
    )
{
    unique_fnmp_handle Handle;
    TEST_HRESULT(FnMpOpenShared(IfIndex, &Handle));
    return Handle;
}

static
unique_fnmp_handle
MpOpenAdapter(
    _In_ UINT32 IfIndex
    )
{
    unique_fnmp_handle Handle;
    TEST_HRESULT(FnMpOpenExclusive(IfIndex, &Handle));
    return Handle;
}

static
unique_fnlwf_handle
LwfOpenDefault(
    _In_ UINT32 IfIndex
    )
{
    unique_fnlwf_handle Handle;
    HRESULT Result;

    //
    // The LWF may not be bound immediately after an adapter restart completes,
    // so poll for readiness.
    //

    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    do {
        Result = FnLwfOpenDefault(IfIndex, &Handle);
        if (SUCCEEDED(Result)) {
            break;
        } else {
            TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), Result);
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_HRESULT(Result);

    return Handle;
}

static
BOOLEAN
LwfIsDatapathActive(
    _In_ const unique_fnlwf_handle &Handle
    )
{
    BOOLEAN IsDatapathActive;

    TEST_HRESULT(FnLwfDatapathGetState(Handle.get(), &IsDatapathActive));

    return IsDatapathActive;
}

struct RX_FRAME {
    DATA_FRAME Frame;
    DATA_BUFFER SingleBufferStorage;

    //
    // Need to explicitly state default constructor when deleting other
    // constructors.
    //
    RX_FRAME() = default;

    //
    // Delete the move and copy constructors. There are cases where a pointer
    // inside of Frame points directly to SingleBufferStorage, and any of these
    // constructors default implementations being called would break this
    // invariant.
    //
    RX_FRAME(const RX_FRAME&) = delete;
    RX_FRAME(RX_FRAME&&) = delete;

    RX_FRAME& operator=(const RX_FRAME&) = delete;
    RX_FRAME& operator=(RX_FRAME&&) = delete;
};

[[nodiscard]]
static
HRESULT
MpRxEnqueueFrame(
    _In_ const unique_fnmp_handle &Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    return FnMpRxEnqueue(Handle.get(), &RxFrame->Frame);
}

[[nodiscard]]
static
HRESULT
TryMpRxFlush(
    _In_ const unique_fnmp_handle &Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    return FnMpRxFlush(Handle.get(), Options);
}

static
VOID
MpRxFlush(
    _In_ const unique_fnmp_handle &Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    HRESULT Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Retry if the interface is not ready: the NDIS data path may be paused.
    //
    do {
        Result = TryMpRxFlush(Handle, Options);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_READY)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_HRESULT(Result);
}

[[nodiscard]]
static
HRESULT
MpRxIndicateFrame(
    _In_ const unique_fnmp_handle &Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    HRESULT Status = MpRxEnqueueFrame(Handle, RxFrame);
    if (!SUCCEEDED(Status)) {
        return Status;
    }
    return TryMpRxFlush(Handle);
}

static
VOID
RxInitializeFrame(
    _Out_ RX_FRAME *Frame,
    _In_ UINT32 HashQueueId,
    _In_reads_(BuffersCount)
        DATA_BUFFER *Buffers,
    _In_ UINT16 BuffersCount
    )
{
    RtlZeroMemory(Frame, sizeof(*Frame));
    Frame->Frame.BufferCount = BuffersCount;
    Frame->Frame.Input.RssHashQueueId = HashQueueId;
    Frame->Frame.Buffers = Buffers;
}

static
VOID
RxInitializeFrame(
    _Out_ RX_FRAME *Frame,
    _In_ UINT32 HashQueueId,
    _In_ DATA_BUFFER *Buffer
    )
{
    RtlZeroMemory(Frame, sizeof(*Frame));
    Frame->Frame.BufferCount = 1;
    Frame->Frame.Input.RssHashQueueId = HashQueueId;
    Frame->Frame.Buffers = Buffer;
}

static
VOID
RxInitializeFrame(
    _Out_ RX_FRAME *Frame,
    _In_ UINT32 HashQueueId,
    _In_ const UCHAR *FrameBuffer,
    _In_ UINT32 FrameLength
    )
{
    RtlZeroMemory(Frame, sizeof(*Frame));
    Frame->Frame.BufferCount = 1;
    Frame->Frame.Input.RssHashQueueId = HashQueueId;
    Frame->Frame.Buffers = nullptr;
    Frame->SingleBufferStorage.DataOffset = 0;
    Frame->SingleBufferStorage.DataLength = FrameLength;
    Frame->SingleBufferStorage.BufferLength = FrameLength;
    Frame->SingleBufferStorage.VirtualAddress = FrameBuffer;
    Frame->Frame.Buffers = &Frame->SingleBufferStorage;
}

[[nodiscard]]
static
unique_fnmp_filter_handle
MpTxFilter(
    _In_ const unique_fnmp_handle &Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_HRESULT(FnMpTxFilter(Handle.get(), Pattern, Mask, Length));

    return unique_fnmp_filter_handle(Handle.get());
}

static
HRESULT
MpTxGetFrame(
    _In_ const unique_fnmp_handle &Handle,
    _In_ UINT32 Index,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    return FnMpTxGetFrame(Handle.get(), Index, 0, FrameBufferLength, Frame);
}

static
VOID
MpTxVerifyNoFrame(
    _In_ const unique_fnmp_handle &Handle,
    _In_ UINT32 Index
    )
{
    UINT32 FrameBufferLength = 0;
    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        MpTxGetFrame(Handle, Index, &FrameBufferLength, NULL));
}

static
unique_malloc_ptr<DATA_FRAME>
MpTxAllocateAndGetFrame(
    _In_ const unique_fnmp_handle &Handle,
    _In_ UINT32 Index
    )
{
    unique_malloc_ptr<DATA_FRAME> FrameBuffer;
    UINT32 FrameLength = 0;
    HRESULT Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = MpTxGetFrame(Handle, Index, &FrameLength, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
    TEST_TRUE(FrameLength >= sizeof(DATA_FRAME));
    FrameBuffer.reset((DATA_FRAME *)AllocMem(FrameLength));
    TEST_NOT_NULL(FrameBuffer.get());

    TEST_HRESULT(MpTxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get()));

    return FrameBuffer;
}

static
VOID
MpTxDequeueFrame(
    _In_ const unique_fnmp_handle &Handle,
    _In_ UINT32 Index
    )
{
    HRESULT Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = FnMpTxDequeueFrame(Handle.get(), Index);
    } while (!Watchdog.IsExpired() && Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND));

    TEST_HRESULT(Result);
}

static
VOID
MpTxFlush(
    _In_ const unique_fnmp_handle &Handle
    )
{
    TEST_HRESULT(FnMpTxFlush(Handle.get()));
}

static
VOID
LwfTxEnqueue(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ DATA_FRAME *Frame
    )
{
    TEST_HRESULT(FnLwfTxEnqueue(Handle.get(), Frame));
}

static
VOID
LwfTxFlush(
    _In_ const unique_fnlwf_handle &Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    HRESULT Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Retry if the interface is not ready: the NDIS data path may be paused.
    //
    do {
        Result = FnLwfTxFlush(Handle.get(), Options);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_READY)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_HRESULT(Result);
}

[[nodiscard]]
static
unique_fnlwf_filter_handle
LwfRxFilter(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_HRESULT(FnLwfRxFilter(Handle.get(), Pattern, Mask, Length));

    return unique_fnlwf_filter_handle(Handle.get());
}

static
HRESULT
LwfRxGetFrame(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ UINT32 Index,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    return FnLwfRxGetFrame(Handle.get(), Index, FrameBufferLength, Frame);
}

static
unique_malloc_ptr<DATA_FRAME>
LwfRxAllocateAndGetFrame(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ UINT32 Index
    )
{
    unique_malloc_ptr<DATA_FRAME> FrameBuffer;
    UINT32 FrameLength = 0;
    HRESULT Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNLWF for RX: the driver doesn't support overlapped IO.
    //
    do {
        Result = LwfRxGetFrame(Handle, Index, &FrameLength, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
    TEST_TRUE(FrameLength >= sizeof(DATA_FRAME));
    FrameBuffer.reset((DATA_FRAME *)AllocMem(FrameLength));
    TEST_NOT_NULL(FrameBuffer.get());

    TEST_HRESULT(LwfRxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get()));

    return FrameBuffer;
}

static
VOID
LwfRxDequeueFrame(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ UINT32 Index
    )
{
    HRESULT Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNLWF for RX: the driver doesn't support overlapped IO.
    //
    do {
        Result = FnLwfRxDequeueFrame(Handle.get(), Index);
    } while (!Watchdog.IsExpired() && Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND));

    TEST_HRESULT(Result);
}

static
VOID
LwfRxFlush(
    _In_ const unique_fnlwf_handle &Handle
    )
{
    TEST_HRESULT(FnLwfRxFlush(Handle.get()));
}

static
HRESULT
LwfOidSubmitRequest(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    )
{
    return
        FnLwfOidSubmitRequest(
            Handle.get(), Key, InformationBufferLength, InformationBuffer);
}

template <typename T>
static
unique_malloc_ptr<T>
LwfOidAllocateAndSubmitRequest(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ OID_KEY Key,
    _Out_ UINT32 *BytesReturned
    )
{
    unique_malloc_ptr<T> InformationBuffer;
    UINT32 InformationBufferLength = 0;
    HRESULT Result;

    Result = LwfOidSubmitRequest(Handle, Key, &InformationBufferLength, NULL);
    TEST_EQUAL(Result, HRESULT_FROM_WIN32(ERROR_MORE_DATA));
    TEST_TRUE(InformationBufferLength > 0);

    InformationBuffer.reset((T *)AllocMem(InformationBufferLength));
    TEST_NOT_NULL(InformationBuffer.get());

    Result = LwfOidSubmitRequest(Handle, Key, &InformationBufferLength, InformationBuffer.get());
    TEST_HRESULT(Result);
    TEST_TRUE(InformationBufferLength > 0);
    *BytesReturned = InformationBufferLength;

    return InformationBuffer;
}

static
VOID
LwfStatusSetFilter(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ NDIS_STATUS StatusCode,
    _In_ BOOLEAN BlockIndications,
    _In_ BOOLEAN QueueIndications
    )
{
    TEST_HRESULT(
        FnLwfStatusSetFilter(Handle.get(), StatusCode, BlockIndications, QueueIndications));
}

static
HRESULT
LwfStatusGetIndication(
    _In_ const unique_fnlwf_handle &Handle,
    _Inout_ UINT32 *StatusBufferLength,
    _Out_writes_bytes_opt_(*StatusBufferLength) VOID *StatusBuffer
    )
{
    return FnLwfStatusGetIndication(Handle.get(), StatusBufferLength, StatusBuffer);
}

template <typename T>
static
unique_malloc_ptr<T>
LwfStatusAllocateAndGetIndication(
    _In_ const unique_fnlwf_handle &Handle,
    _Out_ UINT32 *StatusBufferLength
    )
{
    HRESULT Result;
    unique_malloc_ptr<T> StatusBuffer;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    *StatusBufferLength = 0;

    //
    // Poll FNLWF for status indication: the driver doesn't support overlapped IO.
    //
    do {
        Result = LwfStatusGetIndication(Handle, StatusBufferLength, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    if (SUCCEEDED(Result)) {
        TEST_EQUAL(0, *StatusBufferLength);
    } else {
        TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
        TEST_NOT_EQUAL(0, *StatusBufferLength);

        StatusBuffer.reset((T *)AllocMem(*StatusBufferLength));
        TEST_NOT_NULL(StatusBuffer.get());

        TEST_HRESULT(LwfStatusGetIndication(Handle, StatusBufferLength, StatusBuffer.get()));
    }

    return StatusBuffer;
}

static
LARGE_INTEGER
MpGetLastMiniportPauseTimestamp(
    _In_ const unique_fnmp_handle &Handle
    )
{
    LARGE_INTEGER Timestamp = {0};
    TEST_HRESULT(FnMpGetLastMiniportPauseTimestamp(Handle.get(), &Timestamp));
    return Timestamp;
}

[[nodiscard]]
static
unique_fnmp_mtu_handle
MpSetMtu(
    _In_ const TestInterface &If,
    _In_ const unique_fnmp_handle &Handle,
    _In_ UINT32 Mtu
    )
{
    TEST_HRESULT(FnMpSetMtu(Handle.get(), Mtu));
    return unique_fnmp_mtu_handle(&If);
}

[[nodiscard]]
static
unique_fnmp_mtu_handle
MpSetMtuAndWaitForNdis(
    _In_ const TestInterface &If,
    _In_ const unique_fnmp_handle &Handle,
    _In_ UINT32 Mtu
    )
{
    unique_fnmp_mtu_handle MtuHandle = MpSetMtu(If, Handle, Mtu);

    //
    // Wait for the MTU changes to quiesce. If filter or protocol drivers
    // incompatible with MTU changes are installed, this requires a complete
    // detach/attach cycle of the entire interface stack.
    //
    WaitForNdisDatapath(If, MP_RESTART_TIMEOUT_MS);

    return MtuHandle;
}

static
VOID
MpResetMtu(
    _In_ const TestInterface *If
    )
{
    MpSetMtuAndWaitForNdis(*If, MpOpenGeneric(If->GetIfIndex()), FNMP_DEFAULT_MTU).release();
    WaitForWfpQuarantine(*If);
}

static
VOID
MpOidFilter(
    _In_ const unique_fnmp_handle &Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    )
{
    TEST_HRESULT(FnMpOidFilter(Handle.get(), Keys, KeyCount));
}

static
HRESULT
MpOidGetRequest(
    _In_ const unique_fnmp_handle &Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Out_opt_ VOID *InformationBuffer
    )
{
    return FnMpOidGetRequest(Handle.get(), Key, InformationBufferLength, InformationBuffer);
}

static
unique_malloc_ptr<VOID>
MpOidAllocateAndGetRequest(
    _In_ const unique_fnmp_handle &Handle,
    _In_ OID_KEY Key,
    _Out_ UINT32 *InformationBufferLength,
    _In_opt_ UINT64 Timeout = TEST_TIMEOUT_ASYNC_MS
    )
{
    unique_malloc_ptr<VOID> InformationBuffer;
    UINT32 Length = 0;
    HRESULT Result;
    Stopwatch Watchdog(Timeout);

    //
    // Poll FNMP for an OID: the driver doesn't support overlapped IO.
    //
    do {
        Result = MpOidGetRequest(Handle, Key, &Length, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
    TEST_TRUE(Length > 0);
    InformationBuffer.reset(AllocMem(Length));
    TEST_NOT_NULL(InformationBuffer.get());

    TEST_HRESULT(MpOidGetRequest(Handle, Key, &Length, InformationBuffer.get()));

    *InformationBufferLength = Length;
    return InformationBuffer;
}

static
HRESULT
MpOidCompleteRequest(
    _In_ const unique_fnmp_handle &Handle,
    _In_ OID_KEY Key,
    _In_ NDIS_STATUS Status,
    _In_opt_ const VOID *InformationBuffer,
    _In_ UINT32 InformationBufferLength
    )
{
    return
        FnMpOidCompleteRequest(
            Handle.get(), Key, Status, InformationBuffer, InformationBufferLength);
}

static
VOID
InitializeOffloadParameters(
    _Out_ NDIS_OFFLOAD_PARAMETERS *OffloadParameters
    )
{
    ZeroMemory(OffloadParameters, sizeof(*OffloadParameters));
    OffloadParameters->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;

    if (OsVersionIsVbOrLater()) {
        OffloadParameters->Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5;
        OffloadParameters->Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_5;
    } else {
        OffloadParameters->Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_4;
        OffloadParameters->Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_4;
    }
}

[[nodiscard]]
static
unique_fnmp_task_offload_handle
MpUpdateTaskOffload(
    _In_ const unique_fnmp_handle &Handle,
    _In_ FN_OFFLOAD_TYPE OffloadType,
    _In_ const NDIS_OFFLOAD_PARAMETERS *OffloadParameters,
    _In_opt_ const FN_OFFLOAD_OPTIONS *OffloadOptions = NULL
    )
{
    UINT32 Size = OffloadParameters != NULL ? sizeof(*OffloadParameters) : 0;

    TEST_HRESULT(
        FnMpUpdateTaskOffload2(
            Handle.get(), OffloadType, OffloadParameters, Size,
            const_cast<FN_OFFLOAD_OPTIONS *>(OffloadOptions)));

    return unique_fnmp_task_offload_handle(Handle.get());
}

static
VOID
MpTaskOffloadReset(
    _In_ const FNMP_HANDLE Handle
    )
{
    NDIS_OFFLOAD_PARAMETERS OffloadParams;

    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.LsoV2IPv4 = NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED;
    OffloadParams.LsoV2IPv6 = NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED;
    OffloadParams.IPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED;
    OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED;
    OffloadParams.UDPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED;
    OffloadParams.TCPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED;
    OffloadParams.TCPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED;

    TEST_HRESULT(FnMpUpdateTaskOffload(
        Handle, FnOffloadHardwareCapabilities, &OffloadParams, sizeof(OffloadParams)));
    TEST_HRESULT(FnMpUpdateTaskOffload(
        Handle, FnOffloadCurrentConfig, &OffloadParams, sizeof(OffloadParams)));
}

static
unique_fnsock
CreateUdpSocket(
    _In_ ADDRESS_FAMILY Af,
    _In_opt_ const TestInterface *If,
    _Out_ UINT16 *LocalPort
    )
{
    if (If != NULL) {
        //
        // Ensure the local UDP stack has finished initializing the interface.
        //
        WaitForWfpQuarantine(*If);
    }

    unique_fnsock Socket;
    TEST_HRESULT(FnSockCreate(Af, SOCK_DGRAM, IPPROTO_UDP, &Socket));
    TEST_NOT_NULL(Socket.get());

    SOCKADDR_INET Address = {0};
    Address.si_family = Af;
    TEST_HRESULT(FnSockBind(Socket.get(), (SOCKADDR *)&Address, sizeof(Address)));

    INT AddressLength = sizeof(Address);
    TEST_HRESULT(FnSockGetSockName(Socket.get(), (SOCKADDR *)&Address, &AddressLength));

    INT TimeoutMs = TEST_TIMEOUT_ASYNC_MS;
    TEST_HRESULT(
        FnSockSetSockOpt(
            Socket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));

    *LocalPort = SS_PORT(&Address);
    return Socket;
}

static
unique_fnsock
CreateTcpSocket(
    _In_ ADDRESS_FAMILY Af,
    _In_ const TestInterface *If,
    _Out_ UINT16 *LocalPort,
    _In_ UINT16 RemotePort,
    _Out_ UINT32 *AckNum
    )
{
    //
    // Wait for WFP rules to be plumbed.
    //
    WaitForWfpQuarantine(*If);

    unique_fnsock Socket;
    TEST_HRESULT(FnSockCreate(Af, SOCK_STREAM, IPPROTO_TCP, &Socket));
    TEST_NOT_NULL(Socket.get());

    SOCKADDR_INET Address = {0};
    Address.si_family = Af;
    TEST_HRESULT(FnSockBind(Socket.get(), (SOCKADDR *)&Address, sizeof(Address)));

    INT AddressLength = sizeof(Address);
    TEST_HRESULT(FnSockGetSockName(Socket.get(), (SOCKADDR *)&Address, &AddressLength));

    INT TimeoutMs = TEST_TIMEOUT_ASYNC_MS;
    TEST_HRESULT(
        FnSockSetSockOpt(
            Socket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));

    //
    // Use RST to close local TCP sockets by default. This prevents closed
    // sockets from retransmitting FINs beyond the scope of the uinque_socket.
    //
    LINGER lingerInfo;
    lingerInfo.l_onoff = 1;
    lingerInfo.l_linger = 0;
    TEST_HRESULT(
        FnSockSetSockOpt(Socket.get(), SOL_SOCKET, SO_LINGER, (char *)&lingerInfo, sizeof(lingerInfo)));

    *LocalPort = SS_PORT(&Address);

    //
    // For TCP, emulate handshake to the socket we just created.
    //
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto GenericMp = MpOpenGeneric(If->GetIfIndex());
    wil::unique_handle ProgramHandleTx;

    auto Xsk =
        CreateAndActivateSocket(
            If->GetIfIndex(), If->GetQueueId(), TRUE, FALSE, XDP_GENERIC, XSK_BIND_FLAG_NONE,
            &XdpInspectTxL2);

    XDP_RULE RuleTx;
    RuleTx.Match = XDP_MATCH_TCP_DST;
    RuleTx.Pattern.Port = RemotePort;
    RuleTx.Action = XDP_PROGRAM_ACTION_REDIRECT;
    RuleTx.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    RuleTx.Redirect.Target = Xsk.Handle.get();

    ProgramHandleTx =
        CreateXdpProg(
            If->GetIfIndex(), &XdpInspectTxL2, If->GetQueueId(), XDP_GENERIC,
            &RuleTx, 1);

    If->GetHwAddress(&LocalHw);
    If->GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If->GetIpv4Address(&LocalIp.Ipv4);
        If->GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If->GetIpv6Address(&LocalIp.Ipv6);
        If->GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    TEST_HRESULT(FnSockListen(Socket.get(), 512));

    UCHAR TcpFrame[TCP_HEADER_STORAGE];

    UINT32 TcpFrameLength = sizeof(TcpFrame);
    TEST_TRUE(
        PktBuildTcpFrame(
            TcpFrame, &TcpFrameLength, NULL, 0, NULL, 0, 0, 0, TH_SYN, 65535, &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, *LocalPort, RemotePort));

    SocketProduceRxFill(&Xsk, 1);

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If->GetQueueId(), TcpFrame, TcpFrameLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    //
    // Verify the SYN+ACK has been redirected to XSK, filtering out other
    // TCP connections using the same remote port.
    //

    TCP_HDR *TcpHeaderParsed = NULL;
    Stopwatch Watchdog(5000);
    do {
        UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1, Watchdog.Remaining());
        TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
        auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex++);

        TEST_TRUE(PktParseTcpFrame(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            RxDesc->Length, &TcpHeaderParsed, NULL, 0));

        if (*LocalPort == TcpHeaderParsed->th_sport) {
            break;
        }

        XskRingConsumerRelease(&Xsk.Rings.Rx, 1);
        SocketProduceRxFill(&Xsk, 1);
        TcpHeaderParsed = NULL;
    } while (!Watchdog.IsExpired());

    TEST_NOT_NULL(TcpHeaderParsed);
    TEST_EQUAL(*LocalPort, TcpHeaderParsed->th_sport);
    TEST_EQUAL(TH_SYN | TH_ACK, TcpHeaderParsed->th_flags & (TH_SYN | TH_ACK | TH_RST | TH_FIN));

    //
    // Construct and inject the ACK for SYN+ACK.
    //
    UINT32 AckNumForSynAck = ntohl(TcpHeaderParsed->th_seq) + 1;
    TEST_TRUE(
        PktBuildTcpFrame(
            TcpFrame, &TcpFrameLength, NULL, 0, NULL, 0, 1, AckNumForSynAck, TH_ACK, 65535, &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, *LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If->GetQueueId(), TcpFrame, TcpFrameLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    //
    // Start a blocking accept and ensure that if the accept fails to complete
    // within the timeout, the listening socket gets closed, which will cancel
    // the accept request.
    //
    struct TCP_ACCEPT_THREAD_CONTEXT {
        FNSOCK_HANDLE ListeningSocket;
        FNSOCK_HANDLE AcceptedSocket;
    } Ctx;
    Ctx.ListeningSocket = Socket.get();

    CxPlatAsyncT<TCP_ACCEPT_THREAD_CONTEXT> Async([](TCP_ACCEPT_THREAD_CONTEXT * Ctx) {
        SOCKADDR_INET Address;
        INT AddressLength = sizeof(Address);
        Ctx->AcceptedSocket = FnSockAccept(Ctx->ListeningSocket, (SOCKADDR *)&Address, &AddressLength);
    }, &Ctx);

    TEST_TRUE(Async.WaitFor(TEST_TIMEOUT_ASYNC_MS));

    unique_fnsock AcceptedSocket{Ctx.AcceptedSocket};
    TEST_NOT_NULL(AcceptedSocket.get());

    *AckNum = AckNumForSynAck;
    return AcceptedSocket;
}

static
VOID
WaitForWfpQuarantine(
    _In_ const TestInterface &If
    )
{
    //
    // Restarting the adapter churns WFP filter add/remove.
    // Ensure that our firewall rule is plumbed before we exit this test case.
    //
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv4Address(&LocalIp.Ipv4);
    If.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UCHAR UdpPayload[] = "WaitForWfpQuarantine";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    //
    // On older Windows builds, WFP takes a very long time to de-quarantine.
    //
    Stopwatch Watchdog(30000);
    DWORD Bytes;
    do {
        RX_FRAME RxFrame;
        RxInitializeFrame(&RxFrame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        if (SUCCEEDED(MpRxIndicateFrame(GenericMp, &RxFrame))) {
            Bytes = FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0);
        } else {
            Bytes = (DWORD)-1;
        }

        if (Bytes == sizeof(UdpPayload)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_EQUAL(Bytes, sizeof(UdpPayload));
}

static
BOOLEAN
TryWaitForNdisDatapath(
    _In_ const TestInterface &If,
    _In_ UINT64 TimeoutInMs
    )
{
    CHAR CmdBuff[256];
    BOOLEAN AdapterUp = FALSE;
    BOOLEAN LwfUp = FALSE;
    Stopwatch Watchdog(TimeoutInMs);

    //
    // Wait for the adapter to be "Up", which implies the adapter's data path
    // has been started, which implies the miniport's datapath is active, which
    // in turn implies XDP has finished binding to the NIC.
    //
    // Wait for the functional LWF above XDP to be unpaused.
    //
    // Together, while this does not contractually imply the XDP data path is
    // unpaused, since the NDIS components above and below XDP are both active,
    // this is the best heuristic we have to determine XDP itself is also up.
    //

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));

    do {
        sprintf_s(
            CmdBuff,
            "%s /c exit (Get-NetAdapter -InterfaceDescription '%s').Status -eq 'Up'",
            PowershellPrefix, If.GetIfDesc());
        AdapterUp = !!InvokeSystem(CmdBuff);

        unique_fnlwf_handle FnLwf = LwfOpenDefault(If.GetIfIndex());
        LwfUp = LwfIsDatapathActive(FnLwf);
    } while (CxPlatSleep(TEST_TIMEOUT_ASYNC_MS / 10), !(AdapterUp && LwfUp) && !Watchdog.IsExpired());

    if (!AdapterUp) {
        TraceError("AdapterUp=FALSE");
        return FALSE;
    }

    if (!LwfUp) {
        TraceError("LwfUp=FALSE");
        return FALSE;
    }

    return TRUE;
}

static
VOID
WaitForNdisDatapath(
    _In_ const TestInterface &If,
    _In_ UINT64 TimeoutInMs
    )
{
    TEST_TRUE(TryWaitForNdisDatapath(If, TimeoutInMs));
}

static
VOID
ClearMaskedBits(
    _Inout_ XDP_INET_ADDR *Ip,
    _In_ const XDP_INET_ADDR *Mask,
    _In_ ADDRESS_FAMILY Af
    )
{
    if (Af == AF_INET) {
        Ip->Ipv4.s_addr &= Mask->Ipv4.s_addr;
    } else {
        UINT64 *Ip64 = (UINT64 *)Ip;
        const UINT64 *Mask64 = (CONST UINT64 *)Mask;

        Ip64[0] &= Mask64[0];
        Ip64[1] &= Mask64[1];
    }
}

bool
TestSetup()
{
    WPP_INIT_TRACING(NULL);
    TEST_TRUE(CXPLAT_SUCCEEDED(CxPlatInitialize()));
    GetOSVersion();
    PowershellPrefix = GetPowershellPrefix();
    TEST_HRESULT(FnSockInitialize());
    TEST_EQUAL(0, InvokeSystem("netsh advfirewall firewall add rule name=xdpfntest dir=in action=allow protocol=any remoteip=any localip=any"));
    TEST_EQUAL(FnMpLoadApi(&FnMpLoadApiContext), FNMPAPI_STATUS_SUCCESS);
    TEST_EQUAL(FnLwfLoadApi(&FnLwfLoadApiContext), FNLWFAPI_STATUS_SUCCESS);
    WaitForWfpQuarantine(FnMpIf);
    WaitForNdisDatapath(FnMpIf);
    WaitForWfpQuarantine(FnMp1QIf);
    WaitForNdisDatapath(FnMp1QIf);
    return true;
}

bool
TestCleanup()
{
    FnLwfUnloadApi(FnLwfLoadApiContext);
    FnMpUnloadApi(FnMpLoadApiContext);
    TEST_EQUAL(0, InvokeSystem("netsh advfirewall firewall delete rule name=xdpfntest"));
    FnSockUninitialize();
    CxPlatUninitialize();
    WPP_CLEANUP();
    return true;
}

//
// Tests
//

static
VOID
BindingTest(
    _In_ const TestInterface &If,
    _In_ BOOLEAN RestartAdapter
    )
{
    for (auto Case : RxTxTestCases) {
        //
        // Create and close an XSK.
        //
        {
            auto Socket =
                CreateAndActivateSocket(
                    If.GetIfIndex(), If.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);

            if (RestartAdapter) {
                Stopwatch Timer(MP_RESTART_TIMEOUT_MS);
                If.Restart(FALSE);
                TEST_FALSE(Timer.IsExpired());

                TEST_TRUE(TryWaitForNdisDatapath(If));
            }
        }

        //
        // Create an XSK and RX program, then detach the program, then the XSK.
        //
        if (Case.Rx) {
            auto Socket =
                SetupSocket(If.GetIfIndex(), If.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);

            if (RestartAdapter) {
                Stopwatch Timer(MP_RESTART_TIMEOUT_MS);
                If.Restart(FALSE);
                TEST_FALSE(Timer.IsExpired());

                TEST_TRUE(TryWaitForNdisDatapath(If));
            }

            Socket.RxProgram.reset();
            Socket.Handle.reset();
        }

        //
        // Close the XSK handle while the RX program is still attached.
        //
        if (Case.Rx && !RestartAdapter) {
            auto Socket =
                SetupSocket(
                    If.GetIfIndex(), If.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);

            Socket.Handle.reset();
        }
    }

    if (RestartAdapter) {
        WaitForWfpQuarantine(If);
    }
}

VOID
GenericBinding()
{
    BindingTest(FnMpIf, FALSE);
}

VOID
GenericBindingResetAdapter()
{
    BindingTest(FnMpIf, TRUE);
}

VOID
GenericRxNoPoke()
{
    //
    // Generic mode sockets should never need an RX fill poke.
    //
    auto Socket = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
    TEST_FALSE(XskRingProducerNeedPoke(&Socket.Rings.Fill));
}

VOID
GenericRxSingleFrame()
{
    auto Socket = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(FnMpIf.GetIfIndex());

    DATA_BUFFER Buffer = {0};
    const UCHAR BufferVa[] = "GenericRxSingleFrame";

    //
    // Build one NBL and enqueue it in the functional miniport.
    //
    Buffer.DataOffset = 0;
    Buffer.DataLength = sizeof(BufferVa);
    Buffer.BufferLength = Buffer.DataLength;
    Buffer.VirtualAddress = BufferVa;

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));

    //
    // Produce one XSK fill descriptor.
    //
    SocketProduceRxFill(&Socket, 1);

    //
    // Verify XSK hasn't completed a bogus frame.
    //
    UINT32 ConsumerIndex;
    TEST_EQUAL(0, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));

    //
    // Indicate the NBL to NDIS and XDP.
    //
    TEST_HRESULT(TryMpRxFlush(GenericMp));

    //
    // NDIS, XDP, and XSK are not required to indicate the frame to user space
    // immediately. Wait until the frame arrives.
    //
    ConsumerIndex = SocketConsumerReserve(&Socket.Rings.Rx, 1);

    //
    // Verify the NBL propagated correctly to XSK.
    //
    TEST_EQUAL(1, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
    auto RxDesc = SocketGetAndFreeRxDesc(&Socket, ConsumerIndex);
    TEST_EQUAL(Buffer.DataLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Socket.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            Buffer.VirtualAddress + Buffer.DataOffset,
            Buffer.DataLength));
}

VOID
GenericRxBackfillAndTrailer()
{
    auto Socket = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(FnMpIf.GetIfIndex());

    DATA_BUFFER Buffer = {0};
    const UCHAR BufferVa[] = "GenericRxBackfillAndTrailer";

    //
    // Build one NBL and enqueue it in the functional miniport.
    //
    Buffer.DataOffset = 3;
    Buffer.DataLength = 5;
    Buffer.BufferLength = sizeof(BufferVa);
    Buffer.VirtualAddress = BufferVa;

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));

    //
    // Produce one XSK fill descriptor.
    //
    SocketProduceRxFill(&Socket, 1);
    TEST_HRESULT(TryMpRxFlush(GenericMp));

    UINT32 ConsumerIndex = SocketConsumerReserve(&Socket.Rings.Rx, 1);

    //
    // Verify the NBL propagated correctly to XSK.
    //
    TEST_EQUAL(1, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
    auto RxDesc = SocketGetAndFreeRxDesc(&Socket, ConsumerIndex);
    TEST_EQUAL(Buffer.DataLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Socket.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            Buffer.VirtualAddress + Buffer.DataOffset,
            Buffer.DataLength));
}

VOID
GenericRxAllQueueRedirect(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMpIf;
    UINT16 LocalPort;
    UINT16 RemotePort = htons(1234);
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;

    auto Socket = CreateUdpSocket(Af, &If, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    auto Xsk =
        CreateAndActivateSocket(
            If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);

    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_UDP_DST;
    Rule.Pattern.Port = LocalPort;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();

    wil::unique_handle ProgramHandle =
        CreateXdpProg(
            If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1,
            XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES);

    const UCHAR Payload[] = "GenericRxAllQueueRedirect";
    UINT16 PayloadLength = sizeof(Payload);
    UCHAR PacketBuffer[UDP_HEADER_STORAGE + sizeof(Payload)];
    UINT32 PacketBufferLength = sizeof(PacketBuffer);

    SocketProduceRxFill(&Xsk, 2);

    //
    // Indicate a packet on the wrong RX queue.
    //
    RX_FRAME Frame;
    TEST_TRUE(
        PktBuildUdpFrame(
            PacketBuffer, &PacketBufferLength, Payload, PayloadLength, &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId() + 1, PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS * 2);

    UINT32 ConsumerIndex;
    TEST_EQUAL(0, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));

    //
    // Indicate a packet on the right RX queue.
    //
    TEST_TRUE(
        PktBuildUdpFrame(
            PacketBuffer, &PacketBufferLength, Payload, PayloadLength, &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    //
    // Verify we get the packet that's indicated on the RX queue the rule's bound to.
    //
    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(PacketBufferLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            PacketBuffer,
            PacketBufferLength));
}

VOID
GenericRxTcpControl(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMp1QIf;
    UINT16 LocalPort = htons(4321);
    UINT16 RemotePort = htons(1234);
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;

    WaitForWfpQuarantine(If);

    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    auto Xsk =
        CreateAndActivateSocket(
            If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);

    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_TCP_CONTROL_DST;
    Rule.Pattern.Port = LocalPort;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();

    wil::unique_handle ProgramHandle =
        CreateXdpProg(
            If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    const UCHAR Payload[] = "GenericRxTcpControls";
    UINT16 PayloadLength = sizeof(Payload);
    UCHAR PacketBuffer[TCP_HEADER_STORAGE + sizeof(Payload)];
    UINT32 PacketBufferLength = sizeof(PacketBuffer);

    SocketProduceRxFill(&Xsk, 6);

    //
    // Indicate a packet without control flags.
    //
    RX_FRAME Frame;
    TEST_TRUE(
        PktBuildTcpFrame(
            PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
            NULL, 0, 0, 1, TH_ACK, 65535,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS * 2);

    UINT32 ConsumerIndex;
    TEST_EQUAL(0, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));

    //
    // Indicate a SYN packet.
    //
    TEST_TRUE(
        PktBuildTcpFrame(
            PacketBuffer, &PacketBufferLength, NULL, 0,
            NULL, 0, 0, 1, TH_SYN, 65535,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(PacketBufferLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            PacketBuffer,
            PacketBufferLength));
    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);

    //
    // Indicate a SYN+ACK packet.
    //
    TEST_TRUE(
        PktBuildTcpFrame(
            PacketBuffer, &PacketBufferLength, NULL, 0,
            NULL, 0, 0, 1, TH_SYN | TH_ACK, 65535,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(PacketBufferLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            PacketBuffer,
            PacketBufferLength));
    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);

    //
    // Indicate a FIN packet.
    //
    TEST_TRUE(
        PktBuildTcpFrame(
            PacketBuffer, &PacketBufferLength, NULL, 0,
            NULL, 0, 0, 1, TH_FIN, 65535,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(PacketBufferLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            PacketBuffer,
            PacketBufferLength));
    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);

    //
    // Indicate a FIN+ACK packet.
    //
    TEST_TRUE(
        PktBuildTcpFrame(
            PacketBuffer, &PacketBufferLength, NULL, 0,
            NULL, 0, 0, 1, TH_FIN | TH_ACK, 65535,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(PacketBufferLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            PacketBuffer,
            PacketBufferLength));
    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);

    //
    // Indicate a RST packet.
    //
    TEST_TRUE(
        PktBuildTcpFrame(
            PacketBuffer, &PacketBufferLength, NULL, 0,
            NULL, 0, 0, 1, TH_RST, 65535,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(PacketBufferLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            PacketBuffer,
            PacketBufferLength));
    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);

    //
    // Indicate a RST+ACK packet.
    //
    TEST_TRUE(
        PktBuildTcpFrame(
            PacketBuffer, &PacketBufferLength, NULL, 0,
            NULL, 0, 0, 1, TH_RST | TH_ACK, 65535,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(PacketBufferLength, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            PacketBuffer,
            PacketBufferLength));
    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);
}

VOID
GenericRxMatch(
    _In_ ADDRESS_FAMILY Af,
    _In_ XDP_MATCH_TYPE MatchType,
    _In_ BOOLEAN IsUdp
    )
{
    auto If = IsUdp ? FnMpIf : FnMp1QIf;
    UINT16 LocalPort;
    UINT16 RemotePort = htons(1234);
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    UINT32 AckNum = 0;
    UINT32 SeqNum = 1;

    auto Socket =
        IsUdp ?
            CreateUdpSocket(Af, &If, &LocalPort) :
            CreateTcpSocket(Af, &If, &LocalPort, RemotePort, &AckNum);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    wil::unique_handle ProgramHandle;
    unique_malloc_ptr<UINT8> PortSet;

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    const UCHAR GenericPayload[] = "GenericRxMatch";
    const UCHAR QuicLongHdrPayload[40] = {
        0x80, // IsLongHeader
        0x01, 0x00, 0x00, 0x00, // Version
        0x08, // DestCidLength
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // DestCid
        0x08, // SrcCidLength
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // SrcCid
        0x00  // The rest
    };
    const UCHAR QuicShortHdrPayload[20] = {
        0x00, // IsLongHeader
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // DestCid
        0x00 // The rest
    };
    const UCHAR CorrectQuicCid[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };
    const UCHAR IncorrectQuicCid[] = {
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00
    };

    CHAR RecvPayload[sizeof(QuicLongHdrPayload)];
    UCHAR PacketBuffer[TCP_HEADER_STORAGE + sizeof(QuicLongHdrPayload)];
    const UCHAR* Payload;
    UINT16 PayloadLength;
    if (MatchType == XDP_MATCH_QUIC_FLOW_SRC_CID ||
        MatchType == XDP_MATCH_TCP_QUIC_FLOW_SRC_CID) {
        Payload = QuicLongHdrPayload;
        PayloadLength = sizeof(QuicLongHdrPayload);
    } else if (MatchType == XDP_MATCH_QUIC_FLOW_DST_CID ||
               MatchType == XDP_MATCH_TCP_QUIC_FLOW_DST_CID) {
        Payload = QuicShortHdrPayload;
        PayloadLength = sizeof(QuicShortHdrPayload);
    } else {
        Payload = GenericPayload;
        PayloadLength = sizeof(GenericPayload);
    }

    UINT32 PacketBufferLength = sizeof(PacketBuffer);
    if (IsUdp) {
        TEST_TRUE(
            PktBuildUdpFrame(
                PacketBuffer, &PacketBufferLength, Payload, PayloadLength, &LocalHw,
                &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    } else {
        TEST_TRUE(
            PktBuildTcpFrame(
                PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    }

    XDP_RULE Rule = {};
    Rule.Match = MatchType;
    if (MatchType == XDP_MATCH_UDP_DST ||
        MatchType == XDP_MATCH_TCP_DST) {
        Rule.Pattern.Port = LocalPort;
    } else if (MatchType == XDP_MATCH_IPV4_UDP_TUPLE || MatchType == XDP_MATCH_IPV6_UDP_TUPLE) {
        Rule.Pattern.Tuple.SourcePort = RemotePort;
        Rule.Pattern.Tuple.DestinationPort = LocalPort;
        memcpy(&Rule.Pattern.Tuple.SourceAddress, &RemoteIp, sizeof(INET_ADDR));
        memcpy(&Rule.Pattern.Tuple.DestinationAddress, &LocalIp, sizeof(INET_ADDR));
    } else if (MatchType == XDP_MATCH_QUIC_FLOW_SRC_CID ||
               MatchType == XDP_MATCH_QUIC_FLOW_DST_CID ||
               MatchType == XDP_MATCH_TCP_QUIC_FLOW_SRC_CID ||
               MatchType == XDP_MATCH_TCP_QUIC_FLOW_DST_CID) {
        Rule.Pattern.QuicFlow.UdpPort = LocalPort;
        Rule.Pattern.QuicFlow.CidOffset = 2; // Some arbitrary offset.
        Rule.Pattern.QuicFlow.CidLength = 4; // Some arbitrary length.
        memcpy(
            Rule.Pattern.QuicFlow.CidData,
            CorrectQuicCid + Rule.Pattern.QuicFlow.CidOffset,
            Rule.Pattern.QuicFlow.CidLength);
    } else if (MatchType == XDP_MATCH_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV4_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV6_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV4_TCP_PORT_SET ||
               MatchType == XDP_MATCH_IPV6_TCP_PORT_SET) {
        PortSet.reset((UINT8 *)AllocMem(XDP_PORT_SET_BUFFER_SIZE));
        TEST_NOT_NULL(PortSet.get());

        SetBit(PortSet.get(), LocalPort);

        if (MatchType == XDP_MATCH_UDP_PORT_SET) {
            Rule.Pattern.PortSet.PortSet = PortSet.get();
        } else {
            Rule.Pattern.IpPortSet.Address = *(XDP_INET_ADDR *)&LocalIp;
            Rule.Pattern.IpPortSet.PortSet.PortSet = PortSet.get();
        }
    }

    //
    // Verify XDP pass action.
    //
    ProgramHandle.reset();
    Rule.Action = XDP_PROGRAM_ACTION_PASS;

    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_EQUAL(PayloadLength, FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
    SeqNum += PayloadLength;

    //
    // Verify XDP drop action.
    //
    ProgramHandle.reset();
    Rule.Action = XDP_PROGRAM_ACTION_DROP;

    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    if (!IsUdp) {
        TEST_TRUE(
            PktBuildTcpFrame(
                PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
    }
    RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_TRUE(FAILED(FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0)));
    TEST_EQUAL(WSAETIMEDOUT, FnSockGetLastError());

    //
    // Redirect action is implicitly covered by XSK tests.
    //

    if (Rule.Match == XDP_MATCH_UDP_DST || Rule.Match == XDP_MATCH_TCP_DST) {
        //
        // Verify default action (when no rules match) is pass. Test only makes sense when
        // specific port matching is enabled.
        //
        ProgramHandle.reset();
        Rule.Pattern.Port = htons(ntohs(LocalPort) - 1);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
        SeqNum += PayloadLength;
    } else if (Rule.Match == XDP_MATCH_IPV4_UDP_TUPLE || Rule.Match == XDP_MATCH_IPV6_UDP_TUPLE) {
        //
        // Verify source port matching.
        //
        ProgramHandle.reset();
        Rule.Pattern.Tuple.SourcePort = htons(ntohs(RemotePort) - 1);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));

        //
        // Verify destination port matching.
        //
        ProgramHandle.reset();
        Rule.Pattern.Tuple.SourcePort = RemotePort; // Revert previous test change
        Rule.Pattern.Tuple.DestinationPort = htons(ntohs(LocalPort) - 1);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));

        //
        // Verify source address matching.
        //
        ProgramHandle.reset();
        Rule.Pattern.Tuple.DestinationPort = LocalPort; // Revert previous test change
        (*((UCHAR*)&Rule.Pattern.Tuple.SourceAddress))++;

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));

        //
        // Verify destination address matching.
        //
        ProgramHandle.reset();
        (*((UCHAR*)&Rule.Pattern.Tuple.SourceAddress))--; // Revert previous test change
        (*((UCHAR*)&Rule.Pattern.Tuple.DestinationAddress))++;

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
        SeqNum += PayloadLength;
    } else if (Rule.Match == XDP_MATCH_QUIC_FLOW_SRC_CID ||
               Rule.Match == XDP_MATCH_QUIC_FLOW_DST_CID ||
               Rule.Match == XDP_MATCH_TCP_QUIC_FLOW_SRC_CID ||
               Rule.Match == XDP_MATCH_TCP_QUIC_FLOW_DST_CID) {
        //
        // Verify other header QUIC packets don't match.
        //
        if (Rule.Match == XDP_MATCH_QUIC_FLOW_SRC_CID ||
            Rule.Match == XDP_MATCH_TCP_QUIC_FLOW_SRC_CID) {
            Payload = QuicShortHdrPayload;
            PayloadLength = sizeof(QuicShortHdrPayload);
        } else {
            Payload = QuicLongHdrPayload;
            PayloadLength = sizeof(QuicLongHdrPayload);
        }
        PacketBufferLength = sizeof(PacketBuffer);
        if (IsUdp) {
            TEST_TRUE(
                PktBuildUdpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength, &LocalHw,
                    &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        } else {
            TEST_TRUE(
                PktBuildTcpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                    NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                    &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        }

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
        SeqNum += PayloadLength;

        //
        // Revert UDP payload change from test above.
        //
        if (Rule.Match == XDP_MATCH_QUIC_FLOW_SRC_CID ||
            Rule.Match == XDP_MATCH_TCP_QUIC_FLOW_SRC_CID) {
            Payload = QuicLongHdrPayload;
            PayloadLength = sizeof(QuicLongHdrPayload);
        } else {
            Payload = QuicShortHdrPayload;
            PayloadLength = sizeof(QuicShortHdrPayload);
        }
        PacketBufferLength = sizeof(PacketBuffer);
        if (IsUdp) {
            TEST_TRUE(
                PktBuildUdpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength, &LocalHw,
                    &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        } else {
            TEST_TRUE(
                PktBuildTcpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                    NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                    &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        }

        //
        // Verify the CID matching part.
        //
        ProgramHandle.reset();
        memcpy(
            Rule.Pattern.QuicFlow.CidData,
            IncorrectQuicCid + Rule.Pattern.QuicFlow.CidOffset,
            Rule.Pattern.QuicFlow.CidLength);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
        SeqNum += PayloadLength;

        if (!IsUdp) {
            TEST_TRUE(
                PktBuildTcpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                    NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                    &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        }

        //
        // Verify the port matching part.
        //
        ProgramHandle.reset();
        Rule.Pattern.QuicFlow.UdpPort = htons(ntohs(LocalPort) - 1);
        memcpy(
            Rule.Pattern.QuicFlow.CidData,
            CorrectQuicCid + Rule.Pattern.QuicFlow.CidOffset,
            Rule.Pattern.QuicFlow.CidLength);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
        SeqNum += PayloadLength;
    } else if (MatchType == XDP_MATCH_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV4_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV6_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV4_TCP_PORT_SET ||
               MatchType == XDP_MATCH_IPV6_TCP_PORT_SET) {

        //
        // Verify destination port matching.
        //
        TEST_EQUAL(XDP_PROGRAM_ACTION_DROP, Rule.Action);
        ClearBit(PortSet.get(), LocalPort);

        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
        SeqNum += PayloadLength;

        if (!IsUdp) {
            TEST_TRUE(
                PktBuildTcpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                    NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                    &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        }
        SetBit(PortSet.get(), LocalPort);
        RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_TRUE(FAILED(FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0)));
        TEST_EQUAL(WSAETIMEDOUT, FnSockGetLastError());

        if (MatchType == XDP_MATCH_IPV4_UDP_PORT_SET || MatchType == XDP_MATCH_IPV6_UDP_PORT_SET) {
            //
            // Verify destination address matching.
            //
            ProgramHandle.reset();
            (*((UCHAR*)&Rule.Pattern.IpPortSet.Address))++;

            ProgramHandle =
                CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

            RxInitializeFrame(&Frame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
            TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
            TEST_EQUAL(
                PayloadLength,
                 FnSockRecv(
                    Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
            TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
            SeqNum += PayloadLength;
        }
    } else {
        //
        // TODO - Send and validate some non-UDP traffic.
        //
    }
}

VOID
GenericRxMatchIpPrefix(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMpIf;
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    XDP_INET_ADDR LocalIp, RemoteIp;

    auto UdpSocket = CreateUdpSocket(Af, &If, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    wil::unique_handle ProgramHandle;

    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    UCHAR UdpPayload[] = "GenericRxMatchIpPrefix";
    CHAR RecvPayload[sizeof(UdpPayload)] = {0};
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    XDP_RULE Rule;

    if (Af == AF_INET) {
        Rule.Match = XDP_MATCH_IPV4_DST_MASK;
        TEST_EQUAL(1, inet_pton(Af, "255.192.0.0", &Rule.Pattern.IpMask.Mask));
    } else {
        Rule.Match = XDP_MATCH_IPV6_DST_MASK;
        TEST_EQUAL(1, inet_pton(Af, "FC00::0", &Rule.Pattern.IpMask.Mask));
    }

    Rule.Pattern.IpMask.Address = LocalIp;
    ClearMaskedBits(&Rule.Pattern.IpMask.Address, &Rule.Pattern.IpMask.Mask, Af);
    Rule.Action = XDP_PROGRAM_ACTION_DROP;

    //
    // Verify IP prefix match.
    //
    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_TRUE(FAILED(FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0)));
    TEST_EQUAL(WSAETIMEDOUT, FnSockGetLastError());

    //
    // Verify IP prefix mismatch.
    //
    ProgramHandle.reset();
    *(UCHAR *)&Rule.Pattern.IpMask.Address ^= 0xFFu;

    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
}

VOID
GenericRxMatchInnerIpPrefix(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMpIf;
    UINT16 LocalPort = htons(4321);
    UINT16 RemotePort = htons(1234);
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    XDP_INET_ADDR LocalIp, RemoteIp, InnerSrcIp, InnerDstIp;
    XDP_RULE Rule;

    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
        TEST_EQUAL(1, inet_pton(Af, "10.10.10.1", &InnerSrcIp));
        TEST_EQUAL(1, inet_pton(Af, "10.10.10.2", &InnerDstIp));
        TEST_EQUAL(1, inet_pton(Af, "255.255.255.0", &Rule.Pattern.IpMask.Mask));
        Rule.Match = XDP_MATCH_INNER_IPV4_DST_MASK_UDP;
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
        TEST_EQUAL(1, inet_pton(Af, "beef:a:b:c:d:e:1234:5678", &InnerSrcIp));
        TEST_EQUAL(1, inet_pton(Af, "beef:a:b:c:d:e:1234:5679", &InnerDstIp));
        TEST_EQUAL(1, inet_pton(Af, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:0", &Rule.Pattern.IpMask.Mask));
        Rule.Match = XDP_MATCH_INNER_IPV6_DST_MASK_UDP;
    }

    UCHAR UdpPayload[] = "GenericRxMatchInnerIpPrefix";
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    // Add inner IP header
    UCHAR UdpFrameWithInnerIpHeader[sizeof(UdpFrame) + sizeof(IPV6_HEADER)] = {0};
    UINT32 IpHeaderLength = (Af == AF_INET ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER));
    UINT32 UdpOffset = sizeof(ETHERNET_HEADER) + IpHeaderLength;
    UINT32 UdpLength = UdpFrameLength - UdpOffset;
    RtlCopyMemory(UdpFrameWithInnerIpHeader, UdpFrame, sizeof(ETHERNET_HEADER) + IpHeaderLength);
    UINT32 Offset = sizeof(ETHERNET_HEADER) + IpHeaderLength;
    if (Af == AF_INET) {
        IPV4_HEADER *OuterIpHeader = (IPV4_HEADER *)(UdpFrameWithInnerIpHeader + sizeof(ETHERNET_HEADER));
        OuterIpHeader->Protocol = IPPROTO_IPV4;
        OuterIpHeader->HeaderChecksum = 0;
        OuterIpHeader->HeaderChecksum = PktChecksum(0, OuterIpHeader, sizeof(*OuterIpHeader));

        IPV4_HEADER *InnerIpHeader = (IPV4_HEADER *)(UdpFrameWithInnerIpHeader + Offset);
        InnerIpHeader->Version = IPV4_VERSION;
        InnerIpHeader->HeaderLength = 5;
        InnerIpHeader->TimeToLive = 128;
        InnerIpHeader->TotalLength = htons((UINT16)UdpLength);
        InnerIpHeader->Protocol = IPPROTO_UDP;
        InnerIpHeader->SourceAddress = InnerSrcIp.Ipv4;
        InnerIpHeader->DestinationAddress = InnerDstIp.Ipv4;
        InnerIpHeader->HeaderChecksum = PktChecksum(0, InnerIpHeader, sizeof(*InnerIpHeader));
        Offset += sizeof(IPV4_HEADER);
    } else {
        IPV6_HEADER *OuterIpHeader = (IPV6_HEADER *)(UdpFrameWithInnerIpHeader + sizeof(ETHERNET_HEADER));
        OuterIpHeader->NextHeader = IPPROTO_IPV6;

        IPV6_HEADER *InnerIpHeader = (IPV6_HEADER *)(UdpFrameWithInnerIpHeader + Offset);
        InnerIpHeader->VersionClassFlow = IPV6_VERSION;
        InnerIpHeader->PayloadLength = htons((UINT16)UdpLength);
        InnerIpHeader->NextHeader = IPPROTO_UDP;
        InnerIpHeader->HopLimit = 128;
        RtlCopyMemory(&InnerIpHeader->SourceAddress, &InnerSrcIp.Ipv6, sizeof(IN6_ADDR));
        RtlCopyMemory(&InnerIpHeader->DestinationAddress, &InnerDstIp.Ipv6, sizeof(IN6_ADDR));
        Offset += sizeof(IPV6_HEADER);
    }

    RtlCopyMemory(UdpFrameWithInnerIpHeader + Offset, UdpFrame + UdpOffset, UdpLength);
    Offset += UdpLength;

    auto Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();
    RtlCopyMemory(&Rule.Pattern.IpMask.Address, &InnerDstIp, sizeof(InnerDstIp));
    ClearMaskedBits(&Rule.Pattern.IpMask.Address, &Rule.Pattern.IpMask.Mask, Af);
    wil::unique_handle ProgramHandle = CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);
    SocketProduceRxFill(&Xsk, 6);

    //
    // Verify XDP receives the frame because of inner IP prefix match.
    //
    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrameWithInnerIpHeader, Offset);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);
    TEST_EQUAL(Offset, RxDesc->Length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
            UdpFrameWithInnerIpHeader,
            Offset));
    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);

    //
    // Verify XDP does not receie the frme because of inner IP prefix mismatch.
    //
    if (Af == AF_INET) {
        IPV4_HEADER *InnerIpHeader = (IPV4_HEADER *)(UdpFrameWithInnerIpHeader + sizeof(ETHERNET_HEADER) + sizeof(IPV4_HEADER));
        InnerIpHeader->DestinationAddress = {0};
        InnerIpHeader->HeaderChecksum = 0;
        InnerIpHeader->HeaderChecksum = PktChecksum(0, InnerIpHeader, sizeof(*InnerIpHeader));
    } else {
        IPV6_HEADER *InnerIpHeader = (IPV6_HEADER *)(UdpFrameWithInnerIpHeader + sizeof(ETHERNET_HEADER) +  sizeof(IPV6_HEADER));
        InnerIpHeader->DestinationAddress = {0};
    }
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrameWithInnerIpHeader, Offset);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_EQUAL(0, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
}

VOID
GenericRxLowResources()
{
    auto If = FnMpIf;
    ADDRESS_FAMILY Af = AF_INET6;
    UINT16 LocalPort, LocalXskPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;

    auto UdpSocket = CreateUdpSocket(Af, &If, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    auto Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);

    LocalXskPort = htons(ntohs(LocalPort) + 1);
    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv6Address(&LocalIp.Ipv6);
    If.GetRemoteIpv6Address(&RemoteIp.Ipv6);

    UCHAR UdpMatchPayload[] = "GenericRxLowResourcesMatch";
    UCHAR UdpMatchFrame[UDP_HEADER_STORAGE + sizeof(UdpMatchPayload)];
    UINT32 UdpMatchFrameLength = sizeof(UdpMatchFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpMatchFrame, &UdpMatchFrameLength, UdpMatchPayload, sizeof(UdpMatchPayload), &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalXskPort, RemotePort));

    UCHAR UdpNoMatchPayload[] = "GenericRxLowResourcesNoMatch";
    UCHAR UdpNoMatchFrame[UDP_HEADER_STORAGE + sizeof(UdpNoMatchPayload)];
    UINT32 UdpNoMatchFrameLength = sizeof(UdpNoMatchFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpNoMatchFrame, &UdpNoMatchFrameLength, UdpNoMatchPayload, sizeof(UdpNoMatchPayload),
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_UDP_DST;
    Rule.Pattern.Port = LocalXskPort;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();

    wil::unique_handle ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    //
    // Low resource handling requires logically splitting a chain of NBLs and
    // returning the original chain to the caller. The FNMP driver verifies the
    // NDIS contract, and this test verifies observable behavior.
    //
    const UINT32 NumMatchFrames = 4;
    const UINT32 NumNoMatchFrames = 4;

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpMatchFrame, UdpMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpNoMatchFrame, UdpNoMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpNoMatchFrame, UdpNoMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpMatchFrame, UdpMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpNoMatchFrame, UdpNoMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpMatchFrame, UdpMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpMatchFrame, UdpMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpNoMatchFrame, UdpNoMatchFrameLength);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));

    SocketProduceRxFill(&Xsk, NumMatchFrames);

    DATA_FLUSH_OPTIONS FlushOptions = {0};
    FlushOptions.Flags.LowResources = TRUE;
    TEST_HRESULT(TryMpRxFlush(GenericMp, &FlushOptions));

    //
    // Verify the match NBLs propagated correctly to XSK.
    //
    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, NumMatchFrames);
    TEST_EQUAL(NumMatchFrames, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));

    for (UINT32 Index = 0; Index < NumMatchFrames; Index++) {
        auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex++);
        TEST_EQUAL(UdpMatchFrameLength, RxDesc->Length);
        TEST_TRUE(
            RtlEqualMemory(
                Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
                UdpMatchFrame,
                UdpMatchFrameLength));
    }

    //
    // Verify the no-match NBLs propagated past XDP to the local stack.
    //
    CHAR RecvPayload[sizeof(UdpNoMatchPayload)];
    for (UINT32 Index = 0; Index < NumNoMatchFrames; Index++) {
        TEST_EQUAL(
            sizeof(UdpNoMatchPayload),
            FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(UdpNoMatchPayload, RecvPayload, sizeof(UdpNoMatchPayload)));
    }

    TEST_TRUE(FAILED(FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0)));
    TEST_EQUAL(WSAETIMEDOUT, FnSockGetLastError());
}

VOID
GenericRxMultiSocket()
{
    auto If = FnMpIf;
    ADDRESS_FAMILY Af = AF_INET;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    UCHAR UdpMatchPayload[] = "GenericRxMultiSocket";
    struct {
        MY_SOCKET Xsk;
        UINT16 LocalPort;
        UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpMatchPayload)];
        UINT32 UdpFrameLength;
    } Sockets[2];
    XDP_RULE Rules[RTL_NUMBER_OF(Sockets)] = {};

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv4Address(&LocalIp.Ipv4);
    If.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    for (UINT16 Index = 0; Index < RTL_NUMBER_OF(Sockets); Index++) {
        Sockets[Index].Xsk =
            CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
        Sockets[Index].LocalPort = htons(1000 + Index);
        Sockets[Index].UdpFrameLength = sizeof(Sockets[Index].UdpFrame);
        TEST_TRUE(
            PktBuildUdpFrame(
                Sockets[Index].UdpFrame, &Sockets[Index].UdpFrameLength, UdpMatchPayload,
                sizeof(UdpMatchPayload), &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp,
                Sockets[Index].LocalPort, htons(2000)));

        Rules[Index].Match = XDP_MATCH_UDP_DST;
        Rules[Index].Pattern.Port = Sockets[Index].LocalPort;
        Rules[Index].Action = XDP_PROGRAM_ACTION_REDIRECT;
        Rules[Index].Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
        Rules[Index].Redirect.Target = Sockets[Index].Xsk.Handle.get();
    }

    wil::unique_handle ProgramHandle =
        CreateXdpProg(
            If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC,
            Rules, RTL_NUMBER_OF(Rules));

    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    for (UINT16 Index = 0; Index < RTL_NUMBER_OF(Sockets); Index++) {
        auto &Socket = Sockets[Index].Xsk;
        DATA_BUFFER Buffer = {0};

        SocketProduceRxFill(&Socket, 1);

        Buffer.DataOffset = 0;
        Buffer.DataLength = Sockets[Index].UdpFrameLength;
        Buffer.BufferLength = Buffer.DataLength;
        Buffer.VirtualAddress = Sockets[Index].UdpFrame;
        RX_FRAME Frame;
        RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

        UINT32 ConsumerIndex = SocketConsumerReserve(&Socket.Rings.Rx, 1);

        //
        // Verify the NBL propagated correctly to XSK.
        //
        TEST_EQUAL(1, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
        auto RxDesc = SocketGetAndFreeRxDesc(&Socket, ConsumerIndex);
        TEST_EQUAL(Buffer.DataLength, RxDesc->Length);
        TEST_TRUE(
            RtlEqualMemory(
                Socket.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
                Buffer.VirtualAddress + Buffer.DataOffset,
                Buffer.DataLength));
    }
}

VOID
GenericRxMultiProgram()
{
    auto If = FnMpIf;
    ADDRESS_FAMILY Af = AF_INET;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    UCHAR UdpMatchPayload[] = "GenericRxMultiProgram";
    struct {
        MY_SOCKET Xsk;
        wil::unique_handle ProgramHandle;
        UINT16 LocalPort;
        UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpMatchPayload)];
        UINT32 UdpFrameLength;
    } Sockets[4];

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv4Address(&LocalIp.Ipv4);
    If.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    for (UINT16 Index = 0; Index < RTL_NUMBER_OF(Sockets); Index++) {
        XDP_RULE Rule = {};

        Sockets[Index].Xsk =
            CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
        Sockets[Index].LocalPort = htons(1000 + Index);
        Sockets[Index].UdpFrameLength = sizeof(Sockets[Index].UdpFrame);
        TEST_TRUE(
            PktBuildUdpFrame(
                Sockets[Index].UdpFrame, &Sockets[Index].UdpFrameLength, UdpMatchPayload,
                sizeof(UdpMatchPayload), &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp,
                Sockets[Index].LocalPort, htons(2000)));

        Rule.Match = XDP_MATCH_UDP_DST;
        Rule.Pattern.Port = Sockets[Index].LocalPort;
        Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
        Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
        Rule.Redirect.Target = Sockets[Index].Xsk.Handle.get();

        Sockets[Index].ProgramHandle =
            CreateXdpProg(
                If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC,
                &Rule, 1);
    }

    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    for (UINT16 Index = 0; Index < RTL_NUMBER_OF(Sockets); Index++) {
        auto &Socket = Sockets[Index].Xsk;
        DATA_BUFFER Buffer = {0};
        BOOLEAN Detach = Index & 1;

        if (Detach) {
            //
            // Ensure shared programs are properly detached.
            //
            Sockets[Index].ProgramHandle.reset();
        }

        SocketProduceRxFill(&Socket, 1);

        Buffer.DataOffset = 0;
        Buffer.DataLength = Sockets[Index].UdpFrameLength;
        Buffer.BufferLength = Buffer.DataLength;
        Buffer.VirtualAddress = Sockets[Index].UdpFrame;
        RX_FRAME Frame;
        RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));

        //
        // Verify the NBL propagated correctly to XSK.
        //
        if (Detach) {
            UINT32 ConsumerIndex;
            CxPlatSleep(TEST_TIMEOUT_ASYNC_MS);
            TEST_EQUAL(0, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
        } else {
            UINT32 ConsumerIndex = SocketConsumerReserve(&Socket.Rings.Rx, 1);

            TEST_EQUAL(1, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
            auto RxDesc = SocketGetAndFreeRxDesc(&Socket, ConsumerIndex);
            TEST_EQUAL(Buffer.DataLength, RxDesc->Length);
            TEST_TRUE(
                RtlEqualMemory(
                    Socket.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
                    Buffer.VirtualAddress + Buffer.DataOffset,
                    Buffer.DataLength));
        }
    }
}

VOID
GenericRxUdpFragmentQuicShortHeader(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMpIf;
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    UINT32 TotalOffset = 0;

    auto UdpSocket = CreateUdpSocket(Af, &If, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    wil::unique_handle ProgramHandle;

    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    const UCHAR QuicShortHdrUdpPayload[XDP_QUIC_MAX_CID_LENGTH + 10] = { // 21 bytes is a full CID
        0x00, // IsLongHeader
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // DestCid
        0x00 // The rest
    };
    const UCHAR CorrectQuicCid[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    CxPlatVector<DATA_BUFFER> Buffers;
    DATA_BUFFER Buffer = {0};

    XDP_RULE Rules[2];
    Rules[0].Action = XDP_PROGRAM_ACTION_DROP;
    Rules[0].Match = XDP_MATCH_QUIC_FLOW_DST_CID;
    Rules[0].Pattern.QuicFlow.UdpPort = LocalPort;
    Rules[0].Pattern.QuicFlow.CidOffset = 2; // Some arbitrary offset.
    Rules[0].Pattern.QuicFlow.CidLength = 4; // Some arbitrary length.
    memcpy(
        Rules[0].Pattern.QuicFlow.CidData,
        CorrectQuicCid + Rules[0].Pattern.QuicFlow.CidOffset,
        Rules[0].Pattern.QuicFlow.CidLength);
    Rules[1].Action = XDP_PROGRAM_ACTION_PASS;
    Rules[1].Match = XDP_MATCH_ALL;

    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Rules, 2);

    CHAR RecvPayload[sizeof(QuicShortHdrUdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(QuicShortHdrUdpPayload)];
    const UCHAR* UdpPayload;
    UINT16 UdpPayloadLength = 0;
    UdpPayload = QuicShortHdrUdpPayload;

    for (; UdpPayloadLength < sizeof(QuicShortHdrUdpPayload); UdpPayloadLength++) {
        if (UdpPayloadLength ==
                Rules[0].Pattern.QuicFlow.CidOffset +
                Rules[0].Pattern.QuicFlow.CidLength + 1) {
            Rules[0].Action = XDP_PROGRAM_ACTION_PASS;
            Rules[1].Action = XDP_PROGRAM_ACTION_DROP;
            ProgramHandle.reset();
            ProgramHandle =
                CreateXdpProg(
                    If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Rules, 2);
        }

        //
        // Test a full length buffer
        //

        UINT32 UdpFrameLength = sizeof(UdpFrame);
        TEST_TRUE(
            PktBuildUdpFrame(
                UdpFrame, &UdpFrameLength, UdpPayload, UdpPayloadLength, &LocalHw,
                &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

        RX_FRAME RxFrame;
        RxInitializeFrame(&RxFrame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &RxFrame));
        TEST_EQUAL(
            UdpPayloadLength,
            FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        //
        // Test Payload Fragmented at all points
        //

        for (UINT16 FragmentOffset = 0; FragmentOffset < UdpPayloadLength; FragmentOffset++) {
            Buffers.clear();
            TotalOffset = 0;

            RtlZeroMemory(&Buffer, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = UdpFrameLength - (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            TEST_TRUE(Buffers.push_back(Buffer));

            RtlZeroMemory(&Buffer, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            TEST_TRUE(Buffers.push_back(Buffer));

            RxInitializeFrame(&RxFrame, If.GetQueueId(), Buffers.data(), (UINT16)Buffers.size());
            TEST_HRESULT(MpRxIndicateFrame(GenericMp, &RxFrame));
            TEST_EQUAL(
                UdpPayloadLength,
                FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
            TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));
        }
    }
}

VOID
GenericRxUdpFragmentQuicLongHeader(
    _In_ ADDRESS_FAMILY Af,
    _In_ BOOLEAN IsUdp
    )
{
    auto If = IsUdp ? FnMpIf : FnMp1QIf;
    UINT16 LocalPort;
    UINT16 RemotePort = htons(1234);
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    UINT32 TotalOffset = 0;
    UINT32 AckNum = 0;
    UINT32 SeqNum = 1;

    auto Socket =
        IsUdp ?
            CreateUdpSocket(Af, &If, &LocalPort) :
            CreateTcpSocket(Af, &If, &LocalPort, RemotePort, &AckNum);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    wil::unique_handle ProgramHandle;

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    const UCHAR QuicLongHdrUdpPayload[40] = {
        0x80, // IsLongHeader
        0x01, 0x00, 0x00, 0x00, // Version
        0x08, // DestCidLength
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // DestCid
        0x08, // SrcCidLength
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // SrcCid
        0x00  // The rest
    };
    const UINT16 MinQuicHdrLength = 1 + 4 + 1 + 8 + 1 + 8; // Up to the SrcCid
    const UCHAR CorrectQuicCid[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    CxPlatVector<DATA_BUFFER> Buffers;
    DATA_BUFFER Buffer = {0};

    XDP_RULE Rules[2];
    Rules[0].Action = XDP_PROGRAM_ACTION_DROP;
    Rules[0].Match = (IsUdp ? XDP_MATCH_QUIC_FLOW_SRC_CID : XDP_MATCH_TCP_QUIC_FLOW_SRC_CID);
    Rules[0].Pattern.QuicFlow.UdpPort = LocalPort;
    Rules[0].Pattern.QuicFlow.CidOffset = 2; // Some arbitrary offset.
    Rules[0].Pattern.QuicFlow.CidLength = 4; // Some arbitrary length.
    memcpy(
        Rules[0].Pattern.QuicFlow.CidData,
        CorrectQuicCid + Rules[0].Pattern.QuicFlow.CidOffset,
        Rules[0].Pattern.QuicFlow.CidLength);
    Rules[1].Action = XDP_PROGRAM_ACTION_PASS;
    Rules[1].Match = XDP_MATCH_ALL;

    ProgramHandle =
                CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Rules, 2);

    CHAR RecvPayload[sizeof(QuicLongHdrUdpPayload)];
    UCHAR PacketBuffer[TCP_HEADER_STORAGE + sizeof(QuicLongHdrUdpPayload)];
    const UCHAR* Payload;
    UINT16 PayloadLength = (IsUdp ? 0 : 1); // zero-payload TCP packets are not data packets.
    Payload = QuicLongHdrUdpPayload;

    for (; PayloadLength < sizeof(QuicLongHdrUdpPayload); PayloadLength++) {
        if (PayloadLength == MinQuicHdrLength) {
            Rules[0].Action = XDP_PROGRAM_ACTION_PASS;
            Rules[1].Action = XDP_PROGRAM_ACTION_DROP;
            ProgramHandle.reset();
            ProgramHandle =
                CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Rules, 2);
        }

        //
        // Test a full length buffer
        //

        UINT32 PacketBufferLength = sizeof(PacketBuffer);
        if (IsUdp) {
            TEST_TRUE(
                PktBuildUdpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength, &LocalHw,
                    &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        } else {
            TEST_TRUE(
                PktBuildTcpFrame(
                    PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                    NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                    &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        }

        RX_FRAME RxFrame;
        RxInitializeFrame(&RxFrame, If.GetQueueId(), PacketBuffer, PacketBufferLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &RxFrame));
        TEST_EQUAL(
            PayloadLength,
            FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
        TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
        SeqNum += PayloadLength;

        //
        // Test Payload Fragmented at all points
        //

        for (UINT16 FragmentOffset = 0; FragmentOffset < PayloadLength; FragmentOffset++) {

            Buffers.clear();
            TotalOffset = 0;

            if (!IsUdp) {
                TEST_TRUE(
                    PktBuildTcpFrame(
                        PacketBuffer, &PacketBufferLength, Payload, PayloadLength,
                        NULL, 0, SeqNum, AckNum, TH_ACK, 65535,
                        &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
            }

            RtlZeroMemory(&Buffer, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = PacketBufferLength - (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &PacketBuffer[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            TEST_TRUE(Buffers.push_back(Buffer));

            RtlZeroMemory(&Buffer, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &PacketBuffer[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            TEST_TRUE(Buffers.push_back(Buffer));

            RxInitializeFrame(&RxFrame, If.GetQueueId(), Buffers.data(), (UINT16)Buffers.size());
            TEST_HRESULT(MpRxIndicateFrame(GenericMp, &RxFrame));
            TEST_EQUAL(
                PayloadLength,
                FnSockRecv(Socket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
            TEST_TRUE(RtlEqualMemory(Payload, RecvPayload, PayloadLength));
            SeqNum += PayloadLength;
        }
    }
}

typedef struct _GENERIC_RX_FRAGMENT_PARAMS {
    _In_ UINT16 PayloadLength;
    _In_opt_ UINT8 *TcpOptions;
    _In_ UINT8 TcpOptionsLength;
    _In_opt_ UINT8 TcpFlags;
    _In_ UINT16 Backfill;
    _In_ UINT16 DataTrailer;
    _In_ UINT16 Trailer;
    _In_opt_ const UINT32 *SplitIndexes;
    _In_ UINT16 SplitCount;
    _In_ BOOLEAN IsUdp;
    _In_ BOOLEAN IsTxInspect;
    _In_ BOOLEAN LowResources;
    _In_ UINT16 GroSegCount;
    _In_opt_ UINT32 IfMtu;
    _In_ BOOLEAN UseIpNextHeaderMatch;
    _In_ BOOLEAN UseInnerIpPrefixMatch;
    _In_ XDP_RULE_ACTION Action;
    _In_opt_ const NDIS_OFFLOAD_PARAMETERS *OffloadParams;
    _In_opt_ const FN_OFFLOAD_OPTIONS *OffloadOptions;
} GENERIC_RX_FRAGMENT_PARAMS;

static
BOOLEAN
NdisOffloadTxEnabled(
    _In_ UINT8 OffloadValue
    )
{
    return
        OffloadValue == NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED ||
        OffloadValue == NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
}

static
VOID
GenericRxValidateGroToGso(
    _In_ ADDRESS_FAMILY Af,
    _In_ const GENERIC_RX_FRAGMENT_PARAMS *Params,
    _In_ unique_fnmp_handle &GenericMp,
    _In_ const UCHAR *GroPacketBuffer,
    _In_ UINT32 GroPacketLength,
    _In_ DATA_FLUSH_OPTIONS &RxFlushOptions
    )
{
    const UINT8 TcpHdrSize = sizeof(TCP_HDR) + Params->TcpOptionsLength;
    const UINT8 TcpHdrOffset = TCP_HEADER_BACKFILL(Af) - sizeof(TCP_HDR);
    const TCP_HDR *const RscTcp = (const TCP_HDR *)&GroPacketBuffer[TcpHdrOffset];
    const UINT32 TotalTcpHdrSize = TcpHdrOffset + TcpHdrSize;
    const UINT32 IfMtu = Params->IfMtu > 0 ? Params->IfMtu : FNMP_DEFAULT_MTU;
    const UINT16 IfMss = (UINT16)(IfMtu - TotalTcpHdrSize);
    const UINT16 RscMss =
        (UINT16)((std::max(1ui16, Params->PayloadLength) + Params->GroSegCount - 1) /
            Params->GroSegCount);
    const UINT16 TxMss = std::min(IfMss, RscMss);
    //
    // The following two default properties are hard-coded in FNMP and not
    // defined in API headers.
    //
    const UINT16 LsoMinSegmentCount = 2;
    const UINT32 LsoMaxOffloadSize =
        Params->OffloadOptions != NULL ? Params->OffloadOptions->GsoMaxOffloadSize : 0x20000;
    const UINT32 LsoMinOffloadSize = LsoMinSegmentCount * TxMss + 1;
    const UINT8 FinalSegmentFlags = TH_FIN | TH_PSH;
    CxPlatVector<UCHAR> Filter(TotalTcpHdrSize);
    CxPlatVector<UCHAR> Mask(Filter.size(), 0xFF);
    CxPlatVector<UCHAR> FinalPayload;
    UINT16 TotalSegmentCount = 0;

    TEST_NOT_EQUAL(0, Params->GroSegCount);
    TEST_EQUAL(XDP_PROGRAM_ACTION_L2FWD, Params->Action);

    //
    // GSO to GRO for TxInspect is not implemented yet.
    // UDP GRO to GSO is not implemented yet.
    //
    TEST_FALSE(Params->IsTxInspect);
    TEST_FALSE(Params->IsUdp);

    TEST_TRUE(GroPacketLength >= Filter.size());
    memcpy(&Filter[0], GroPacketBuffer, Filter.size());

    //
    // Clear variable fields in the mask. These will be validated in the final
    // GSO'd frames.
    //

    if (Af == AF_INET) {
        IPV4_HEADER *Ipv4 = (IPV4_HEADER *)&Mask[sizeof(ETHERNET_HEADER)];
        Ipv4->TotalLength = 0;
        Ipv4->Identification = 0;
        Ipv4->HeaderChecksum = 0;
    } else {
        IPV6_HEADER *Ipv6 = (IPV6_HEADER *)&Mask[sizeof(ETHERNET_HEADER)];
        Ipv6->PayloadLength = 0;
    }

    TCP_HDR *TcpMask = (TCP_HDR *)&Mask[TcpHdrOffset];
    TcpMask->th_seq = 0;
    TcpMask->th_sum = 0;
    TcpMask->th_flags = (UINT8)~(FinalSegmentFlags);

    auto MpFilter = MpTxFilter(GenericMp, &Filter[0], &Mask[0], (UINT32)Filter.size());
    MpRxFlush(GenericMp, &RxFlushOptions);

    if (Params->SplitCount > 0 && Params->SplitIndexes[0] < TotalTcpHdrSize) {
        //
        // GRO to GSO requires contiguous headers through the TCP header.
        //
        MpTxVerifyNoFrame(GenericMp, 0);
        return;
    }

    //
    // Validate multiple GSO/csum offloaded frames. Require at least one frame,
    // even if there is no payload remaining, to handle the case of pure ACKs.
    //

    do {
        const UINT16 PayloadRemaining = Params->PayloadLength - (UINT16)FinalPayload.size();
        CxPlatVector<UCHAR> FrameData;

        auto TxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
        MpTxDequeueFrame(GenericMp, 0);

        //
        // Verify the entire packet (and nothing more) was forwarded. The TX
        // filter verifies the bytes match up to the length of the filter.
        //
        for (UINT32 i = 0; i < TxFrame->BufferCount; i++) {
            DATA_BUFFER *Buffer = &TxFrame->Buffers[i];
            const UCHAR *PayloadStart = Buffer->VirtualAddress + Buffer->DataOffset;
            FrameData.insert(
                FrameData.end(), PayloadStart, PayloadStart + Buffer->DataLength);
        }

        TEST_TRUE(FrameData.size() >= TotalTcpHdrSize);
        const IPV4_HEADER *const Ipv4 =
            (const IPV4_HEADER *)&FrameData[sizeof(ETHERNET_HEADER)];
        const IPV6_HEADER *const Ipv6 =
            (const IPV6_HEADER *)&FrameData[sizeof(ETHERNET_HEADER)];
        const TCP_HDR *const Tcp = (const TCP_HDR *)&FrameData[TcpHdrOffset];
        const UINT16 TcpPayloadLength = (UINT16)(FrameData.size() - TotalTcpHdrSize);
        const UINT16 SegmentCount =
            (TcpPayloadLength == 0) ? 1 : (UINT16)(TcpPayloadLength + TxMss - 1) / TxMss;
        const VOID *IpSrc;
        const VOID *IpDst;
        UINT8 IpAddressLength;
        UINT16 TcpPseudoHdrCsum;

        if (Af == AF_INET) {
            const IPV4_HEADER *const RscIpv4 =
                (const IPV4_HEADER *)&GroPacketBuffer[sizeof(ETHERNET_HEADER)];

            TEST_EQUAL(
                htons(ntohs(RscIpv4->Identification) + TotalSegmentCount),
                Ipv4->Identification);

            IpSrc = &Ipv4->SourceAddress;
            IpDst = &Ipv4->DestinationAddress;
            IpAddressLength = sizeof(Ipv4->SourceAddress);
        } else {
            IpSrc = &Ipv6->SourceAddress;
            IpDst = &Ipv6->DestinationAddress;
            IpAddressLength = sizeof(Ipv6->SourceAddress);
        }

        TcpPseudoHdrCsum =
            PktPseudoHeaderChecksum(
                IpSrc, IpDst, IpAddressLength, TcpHdrSize + TcpPayloadLength, IPPROTO_TCP);

        TEST_EQUAL(htonl(ntohl(RscTcp->th_seq) + (UINT16)FinalPayload.size()), Tcp->th_seq);

        if (TcpPayloadLength == PayloadRemaining) {
            TEST_EQUAL(RscTcp->th_flags & FinalSegmentFlags, Tcp->th_flags & FinalSegmentFlags);
        } else {
            TEST_FALSE(Tcp->th_flags & FinalSegmentFlags);
        }

        if (TxFrame->Output.Lso.Value != 0) {
            //
            // This is an LSO frame.
            //

            TEST_EQUAL(NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE, TxFrame->Output.Lso.LsoV2Transmit.Type);
            TEST_EQUAL(TxMss, TxFrame->Output.Lso.LsoV2Transmit.MSS);
            TEST_EQUAL(TcpHdrOffset, TxFrame->Output.Lso.LsoV2Transmit.TcpHeaderOffset);
            TEST_EQUAL(0, TxFrame->Output.Checksum.Value);
            TEST_TRUE(SegmentCount >= LsoMinSegmentCount);
            TEST_EQUAL(TcpPseudoHdrCsum, Tcp->th_sum);
            TEST_TRUE(TcpPayloadLength == PayloadRemaining || TcpPayloadLength % TxMss == 0);
            TEST_TRUE(TcpPayloadLength <= LsoMaxOffloadSize);
            TEST_TRUE(TcpPayloadLength >= LsoMinOffloadSize);

            if (Af == AF_INET) {
                TEST_EQUAL(0, Ipv4->TotalLength);
                TEST_EQUAL(0, Ipv4->HeaderChecksum);
                TEST_TRUE(
                    Params->OffloadParams != NULL &&
                    Params->OffloadParams->LsoV2IPv4 == NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED);
                TEST_EQUAL(
                    NDIS_TCP_LARGE_SEND_OFFLOAD_IPv4, TxFrame->Output.Lso.LsoV2Transmit.IPVersion);
            } else {
                TEST_EQUAL(0, Ipv6->PayloadLength);
                TEST_TRUE(
                    Params->OffloadParams != NULL &&
                    Params->OffloadParams->LsoV2IPv6 == NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED);
                TEST_EQUAL(
                    NDIS_TCP_LARGE_SEND_OFFLOAD_IPv6, TxFrame->Output.Lso.LsoV2Transmit.IPVersion);
            }
        } else {
            //
            // This is a regular, non-LSO frame.
            //

            BOOLEAN TcpChecksumOffload = FALSE;

            TEST_TRUE(
                PayloadRemaining < LsoMinOffloadSize ||
                LsoMinSegmentCount * (UINT32)TxMss > LsoMaxOffloadSize ||
                Params->OffloadParams == NULL ||
                (Af == AF_INET &&
                    Params->OffloadParams->LsoV2IPv4 == NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED) ||
                (Af == AF_INET6 &&
                    Params->OffloadParams->LsoV2IPv6 == NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED));
            TEST_EQUAL(1, SegmentCount);
            TEST_TRUE(TcpPayloadLength == PayloadRemaining || TcpPayloadLength == TxMss);
            TEST_FALSE(
                TxFrame->Output.Checksum.Transmit.IsIPv4 &&
                TxFrame->Output.Checksum.Transmit.IsIPv6);

            if (Af == AF_INET) {
                TEST_EQUAL(
                    htons((UINT16)FrameData.size() - sizeof(ETHERNET_HEADER)),
                    Ipv4->TotalLength);

                if (Params->OffloadParams != NULL &&
                    NdisOffloadTxEnabled(Params->OffloadParams->IPv4Checksum)) {
                    TEST_EQUAL(0, Ipv4->HeaderChecksum);
                    TEST_TRUE(TxFrame->Output.Checksum.Transmit.IpHeaderChecksum);
                    TEST_TRUE(TxFrame->Output.Checksum.Transmit.IsIPv4);
                } else {
                    TEST_EQUAL(0ui16, PktChecksum(0, Ipv4, sizeof(*Ipv4)));
                    TEST_FALSE(TxFrame->Output.Checksum.Transmit.IpHeaderChecksum);
                }

                if (Params->OffloadParams != NULL &&
                    NdisOffloadTxEnabled(Params->OffloadParams->TCPIPv4Checksum)) {
                    TcpChecksumOffload = TRUE;
                    TEST_TRUE(TxFrame->Output.Checksum.Transmit.IsIPv4);
                }
            } else {
                TEST_EQUAL(
                    htons((UINT16)FrameData.size() - TcpHdrOffset),
                    Ipv6->PayloadLength);

                if (Params->OffloadParams != NULL &&
                    NdisOffloadTxEnabled(Params->OffloadParams->TCPIPv6Checksum)) {
                    TcpChecksumOffload = TRUE;
                    TEST_TRUE(TxFrame->Output.Checksum.Transmit.IsIPv6);
                }
            }

            TEST_EQUAL(TcpChecksumOffload, TxFrame->Output.Checksum.Transmit.TcpChecksum);

            if (TcpChecksumOffload) {
                TEST_EQUAL(TcpHdrOffset, TxFrame->Output.Checksum.Transmit.TcpHeaderOffset);
                TEST_EQUAL(TcpPseudoHdrCsum, Tcp->th_sum);
            } else {
                TEST_EQUAL(
                    0ui16,
                    PktChecksum(TcpPseudoHdrCsum, Tcp, TcpHdrSize + TcpPayloadLength));
            }
        }

        FinalPayload.insert(
            FinalPayload.end(), FrameData.begin() + TotalTcpHdrSize, FrameData.end());

        TotalSegmentCount += SegmentCount;
    } while (FinalPayload.size() < Params->PayloadLength);

    TEST_EQUAL((std::max(1ui16, Params->PayloadLength) + TxMss - 1) / TxMss, TotalSegmentCount);
    TEST_TRUE(TotalSegmentCount == Params->GroSegCount || IfMss < RscMss);
    TEST_EQUAL(GroPacketLength - TotalTcpHdrSize - Params->DataTrailer, FinalPayload.size());
    TEST_TRUE(
        RtlEqualMemory(
            FinalPayload.data(), &GroPacketBuffer[TotalTcpHdrSize], FinalPayload.size()));

    MpTxFlush(GenericMp);
    MpTxVerifyNoFrame(GenericMp, 0);
}

static
CxPlatVector<DATA_BUFFER>
GenericRxCreateSplitBuffers(
    _In_ const UCHAR *FrameData,
    _In_ UINT32 PacketLength,
    _In_ UINT16 Backfill,
    _In_ UINT16 Trailer,
    _In_opt_count_(SplitCount) const UINT32 *SplitIndexes,
    _In_ UINT16 SplitCount
    )
{
    CxPlatVector<DATA_BUFFER> Buffers;
    UINT32 PacketBufferOffset = 0;
    UINT32 TotalOffset = 0;

    //
    // Split up the frame into RX fragment buffers.
    //
    for (UINT16 Index = 0; Index < SplitCount; Index++) {
        DATA_BUFFER Buffer = {0};

        __analysis_assume(SplitIndexes != NULL);

        Buffer.DataOffset = Index == 0 ? Backfill : 0;
        Buffer.DataLength = SplitIndexes[Index] - PacketBufferOffset;
        Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
        Buffer.VirtualAddress = FrameData + TotalOffset;

        PacketBufferOffset += Buffer.DataLength;
        TotalOffset += Buffer.BufferLength;

        Buffers.push_back(Buffer);
    }

    //
    // Produce the final RX fragment buffer.
    //
    DATA_BUFFER Buffer = {0};
    Buffer.DataOffset = Buffers.size() == 0 ? Backfill : 0;
    Buffer.DataLength = PacketLength - PacketBufferOffset;
    Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength + Trailer;
    Buffer.VirtualAddress = FrameData + TotalOffset;
    Buffers.push_back(Buffer);

    return Buffers;
}

static
VOID
GenericRxFragmentBuffer(
    _In_ ADDRESS_FAMILY Af,
    _In_ const GENERIC_RX_FRAGMENT_PARAMS *Params
    )
{
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    XDP_INET_ADDR InnerSrcIp = { 0 }, InnerDstIp = { 0 };
    MY_SOCKET Xsk;
    wil::unique_handle ProgramHandle;
    unique_fnmp_handle GenericMp;
    unique_fnlwf_handle FnLwf;
    const XDP_HOOK_ID *RxHookId = Params->IsTxInspect ? &XdpInspectTxL2 : &XdpInspectRxL2;
    unique_fnmp_task_offload_handle OffloadReset;
    unique_fnmp_mtu_handle MtuReset;
    const UINT8 ThFlags = Params->TcpFlags != 0 ? Params->TcpFlags : TH_ACK;
    auto If = FnMpIf;
    static const UINT8 TestNextHeaderValue = 0xFD; // Reserved for testing in RFC 3692.

    LocalPort = htons(1234);
    RemotePort = htons(4321);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    if (Params->IsTxInspect) {
        FnLwf = LwfOpenDefault(If.GetIfIndex());
    } else {
        GenericMp = MpOpenGeneric(If.GetIfIndex());

        if (Params->OffloadParams != NULL) {
            OffloadReset =
                MpUpdateTaskOffload(
                    GenericMp, FnOffloadCurrentConfig, Params->OffloadParams,
                    Params->OffloadOptions);
        }

        if (Params->IfMtu > 0) {
            MtuReset = MpSetMtuAndWaitForNdis(If, GenericMp, Params->IfMtu);
        }
    }

    XDP_RULE Rule;
    if (Params->UseIpNextHeaderMatch) {
        Rule.Match = XDP_MATCH_IP_NEXT_HEADER;
        Rule.Pattern.NextHeader = TestNextHeaderValue;
    } else if (Params->UseInnerIpPrefixMatch) {
        if (Af == AF_INET) {
            TEST_EQUAL(1, inet_pton(Af, "10.10.10.1", &InnerSrcIp));
            TEST_EQUAL(1, inet_pton(Af, "10.10.10.2", &InnerDstIp));
            TEST_EQUAL(1, inet_pton(Af, "255.255.255.0", &Rule.Pattern.IpMask.Mask));
            Rule.Match = XDP_MATCH_INNER_IPV4_DST_MASK_UDP;
        } else {
            TEST_EQUAL(1, inet_pton(Af, "beef:a:b:c:d:e:1234:5678", &InnerSrcIp));
            TEST_EQUAL(1, inet_pton(Af, "beef:a:b:c:d:e:1234:5679", &InnerDstIp));
            TEST_EQUAL(1, inet_pton(Af, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:0", &Rule.Pattern.IpMask.Mask));
            Rule.Match = XDP_MATCH_INNER_IPV6_DST_MASK_UDP;
        }

        RtlCopyMemory(&Rule.Pattern.IpMask.Address, &InnerDstIp, sizeof(InnerDstIp));
        ClearMaskedBits(&Rule.Pattern.IpMask.Address, &Rule.Pattern.IpMask.Mask, Af);
    } else {
        Rule.Match = Params->IsUdp ? XDP_MATCH_UDP_DST : XDP_MATCH_TCP_DST;
        Rule.Pattern.Port = LocalPort;
    }
    Rule.Action = Params->Action;

    if (Params->Action == XDP_PROGRAM_ACTION_REDIRECT) {
        Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
        Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
        Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
        Rule.Redirect.Target = Xsk.Handle.get();
    }

    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), RxHookId, If.GetQueueId(), XDP_GENERIC, &Rule, 1);
    //
    // Allocate UDP payload and initialize to a pattern.
    //
    unique_malloc_ptr<UCHAR> Payload;
    if (Params->PayloadLength > 0) {
        Payload.reset((UCHAR *)AllocMem(Params->PayloadLength));
        TEST_NOT_NULL(Payload.get());
        CxPlatRandom(Params->PayloadLength, Payload.get());
    }

    CxPlatVector<UCHAR> PacketBuffer(
        Params->Backfill +
        (Params->IsUdp ?
            UDP_HEADER_BACKFILL(Af) : TCP_HEADER_BACKFILL(Af) + Params->TcpOptionsLength) +
        Params->PayloadLength + Params->DataTrailer + Params->Trailer);
    UINT32 ActualPacketLength =
        (UINT32)PacketBuffer.size() - Params->Backfill - Params->Trailer - Params->DataTrailer;
    if (Params->IsUdp) {
        TEST_TRUE(
            PktBuildUdpFrame(
                PacketBuffer.data() + Params->Backfill, &ActualPacketLength, Payload.get(),
                Params->PayloadLength, &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort,
                RemotePort));

        if (Params->UseInnerIpPrefixMatch) {
            TraceError("UseInnerIpPrefixMatch");
            UINT32 UdpOffset = sizeof(ETHERNET_HEADER) + (Af == AF_INET ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER));
            UCHAR *OuterIpHeaderStart = PacketBuffer.data() + Params->Backfill + sizeof(ETHERNET_HEADER);
            if (Af == AF_INET) {
                IPV4_HEADER *OuterIpHeader = (IPV4_HEADER *)OuterIpHeaderStart;
                OuterIpHeader->Protocol = IPPROTO_IPV4;
                OuterIpHeader->HeaderChecksum = 0;
                OuterIpHeader->HeaderChecksum = PktChecksum(0, OuterIpHeader, sizeof(*OuterIpHeader));
                IPV4_HEADER InnerIpHeader = { 0 };
                InnerIpHeader.Version = IPV4_VERSION;
                InnerIpHeader.HeaderLength = 5;
                InnerIpHeader.TimeToLive = 128;
                InnerIpHeader.TotalLength = htons((UINT16)Params->PayloadLength);
                InnerIpHeader.Protocol = IPPROTO_UDP;
                InnerIpHeader.SourceAddress = InnerSrcIp.Ipv4;
                InnerIpHeader.DestinationAddress = InnerDstIp.Ipv4;
                InnerIpHeader.HeaderChecksum = PktChecksum(0, &InnerIpHeader, sizeof(InnerIpHeader));
                ActualPacketLength += sizeof(IPV4_HEADER);
                PacketBuffer.insert(PacketBuffer.begin() + Params->Backfill + UdpOffset, (UCHAR *)&InnerIpHeader, (UCHAR *)&InnerIpHeader + sizeof(IPV4_HEADER));
            } else {
                IPV6_HEADER *OuterIpHeader = (IPV6_HEADER *)OuterIpHeaderStart;
                OuterIpHeader->NextHeader = IPPROTO_IPV6;
                OuterIpHeader->PayloadLength += sizeof(IPV6_HEADER);
                IPV6_HEADER InnerIpHeader = { 0 };
                InnerIpHeader.VersionClassFlow = IPV6_VERSION;
                InnerIpHeader.PayloadLength = OuterIpHeader->PayloadLength;
                InnerIpHeader.NextHeader = IPPROTO_UDP;
                InnerIpHeader.HopLimit = 128;
                RtlCopyMemory(&InnerIpHeader.SourceAddress, &InnerSrcIp.Ipv6, sizeof(IN6_ADDR));
                RtlCopyMemory(&InnerIpHeader.DestinationAddress, &InnerDstIp.Ipv6, sizeof(IN6_ADDR));
                ActualPacketLength += sizeof(IPV6_HEADER);
                PacketBuffer.insert(PacketBuffer.begin() + Params->Backfill + UdpOffset, (UCHAR *)&InnerIpHeader, (UCHAR *)&InnerIpHeader + sizeof(IPV6_HEADER));
            }
        }
    } else {
        TEST_TRUE(
            PktBuildTcpFrame(
                PacketBuffer.data() + Params->Backfill, &ActualPacketLength, Payload.get(),
                Params->PayloadLength, Params->TcpOptions, Params->TcpOptionsLength, 0xabcd4321,
                0x567890fe, ThFlags, 65535, &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort,
                RemotePort));
    }
    if (Params->UseIpNextHeaderMatch) {
        VOID *IpHeader =
            RTL_PTR_ADD(PacketBuffer.data() + Params->Backfill, sizeof(ETHERNET_HEADER));

        //
        // Rewrite the IP next header field to be a unique value unused by the
        // local stack. Do not fix up checksums. Not compatible with offloads.
        //
        if (Af == AF_INET) {
            IPV4_HEADER *Ipv4 = (IPV4_HEADER *)IpHeader;
            Ipv4->Protocol = TestNextHeaderValue;
        } else {
            TEST_EQUAL(AF_INET6, Af);
            IPV6_HEADER *Ipv6 = (IPV6_HEADER *)IpHeader;
            Ipv6->NextHeader = TestNextHeaderValue;
        }
    }

    ActualPacketLength += Params->DataTrailer;

    CxPlatVector<DATA_BUFFER> Buffers =
        GenericRxCreateSplitBuffers(
            PacketBuffer.data(), ActualPacketLength, Params->Backfill, Params->Trailer,
            Params->SplitIndexes, Params->SplitCount);

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), Buffers.data(), (UINT16)Buffers.size());

    if (Params->IsTxInspect) {
        LwfTxEnqueue(FnLwf, &Frame.Frame);
    } else {
        Frame.Frame.Input.Rsc.Info.CoalescedSegCount = Params->GroSegCount;
        TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    }

    DATA_FLUSH_OPTIONS RxFlushOptions = {0};
    RxFlushOptions.Flags.LowResources = Params->LowResources;

    if (Params->Action == XDP_PROGRAM_ACTION_REDIRECT) {
        //
        // TODO: TX-inspect to XSK test case not implemented.
        //
        TEST_FALSE(Params->IsTxInspect);

        //
        // Produce one XSK fill descriptor.
        //
        SocketProduceRxFill(&Xsk, 1);
        TEST_HRESULT(TryMpRxFlush(GenericMp, &RxFlushOptions));

        UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);

        //
        // Verify the NBL propagated correctly to XSK.
        //
        TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
        auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);

        TEST_EQUAL(ActualPacketLength, RxDesc->Length);
        TEST_TRUE(
            RtlEqualMemory(
                Xsk.Umem.Buffer.get() + RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
                PacketBuffer.data() + Params->Backfill,
                ActualPacketLength));
    } else if (Params->Action == XDP_PROGRAM_ACTION_L2FWD) {
        UCHAR *L2FwdPacket = PacketBuffer.data() + Params->Backfill;
        UINT32 L2FwdPacketLength = ActualPacketLength;
        CxPlatVector<UCHAR> Mask(L2FwdPacketLength, 0xFF);
        ETHERNET_HEADER *Ethernet = (ETHERNET_HEADER *)L2FwdPacket;
        ETHERNET_ADDRESS TempAddress;
        unique_malloc_ptr<DATA_FRAME> TxFrame;
        UINT32 TotalLength = 0;

        //
        // Set a TX filter that matches the entire packet with the ethernet
        // source and destination swapped.
        //
        TempAddress = Ethernet->Destination;
        Ethernet->Destination = Ethernet->Source;
        Ethernet->Source = TempAddress;

        if (Params->GroSegCount > 0) {
            GenericRxValidateGroToGso(
                Af, Params, GenericMp, &L2FwdPacket[0], ActualPacketLength, RxFlushOptions);
            return;
        }

        unique_fnlwf_filter_handle LwfFilter;
        unique_fnmp_filter_handle MpFilter;

        if (Params->IsTxInspect) {
            LwfFilter = LwfRxFilter(FnLwf, L2FwdPacket, Mask.data(), L2FwdPacketLength);
            LwfTxFlush(FnLwf, &RxFlushOptions);
            TxFrame = LwfRxAllocateAndGetFrame(FnLwf, 0);
        } else {
            MpFilter = MpTxFilter(GenericMp, L2FwdPacket, Mask.data(), L2FwdPacketLength);
            MpRxFlush(GenericMp, &RxFlushOptions);
            TxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
        }

        //
        // Verify the entire packet (and nothing more) was forwarded. The TX
        // filter verifies the bytes match up to the length of the filter.
        //
        for (UINT32 i = 0; i < TxFrame->BufferCount; i++) {
            TotalLength += TxFrame->Buffers[i].DataLength;
        }
        TEST_EQUAL(L2FwdPacketLength, TotalLength);

        //
        // Non-low-resources NBLs should be forwarded without a data copy. We
        // infer this is true by the TX NBL having the same MDL layout as the
        // original NBL.
        //
        // For TX-inspect NBLs, allow XDP to copy data if the first buffer is
        // too short to reliably contain all L2 and L3 headers due to NDIS
        // lookahead requirements.
        //
        if (!Params->LowResources &&
            (!Params->IsTxInspect || Frame.Frame.Buffers[0].DataLength > 128)) {
            TEST_EQUAL(Buffers.size(), TxFrame->BufferCount);
        }

        //
        // XDP should preserve the available backfill.
        //
        TEST_EQUAL(Params->Backfill, TxFrame->Buffers[0].DataOffset);

        if (Params->IsTxInspect) {
            LwfRxDequeueFrame(FnLwf, 0);
            LwfRxFlush(FnLwf);
        } else {
            MpTxDequeueFrame(GenericMp, 0);
            MpTxFlush(GenericMp);
        }
    }
}

VOID
GenericRxFragmentHeaderData(
    _In_ ADDRESS_FAMILY Af,
    _In_ BOOLEAN IsUdp
    )
{
    UINT32 SplitIndexes[] = { IsUdp ? UDP_HEADER_BACKFILL(Af) : TCP_HEADER_BACKFILL(Af) };
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Params.IsUdp = IsUdp;
    Params.PayloadLength = 23;
    Params.Backfill = 13;
    Params.Trailer = 17;
    Params.SplitIndexes = SplitIndexes;
    Params.SplitCount = RTL_NUMBER_OF(SplitIndexes);
    GenericRxFragmentBuffer(Af, &Params);
}

VOID
GenericRxTooManyFragments(
    _In_ ADDRESS_FAMILY Af,
    _In_ BOOLEAN IsUdp
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Params.IsUdp = IsUdp;
    Params.PayloadLength = 512;
    Params.Backfill = 13;
    Params.Trailer = 17;
    CxPlatVector<UINT32> SplitIndexes;
    for (UINT16 Index = 0; Index < Params.PayloadLength - 1; Index++) {
        TEST_TRUE(SplitIndexes.push_back(Index + 1));
    }
    Params.SplitIndexes = SplitIndexes.data();
    Params.SplitCount = (UINT16)SplitIndexes.size();
    GenericRxFragmentBuffer(Af, &Params);
}

VOID
GenericRxHeaderMultipleFragments(
    _In_ ADDRESS_FAMILY Af,
    _In_ XDP_RULE_ACTION ProgramAction,
    _In_ BOOLEAN IsUdp,
    _In_ BOOLEAN IsTxInspect,
    _In_ BOOLEAN IsLowResources,
    _In_ BOOLEAN UseIpNextHeaderMatch,
    _In_ BOOLEAN UseInnerIpPrefixMatch
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.Action = ProgramAction;
    Params.IsUdp = IsUdp;
    Params.PayloadLength = 43;
    Params.Backfill = 13;
    Params.Trailer = 17;
    UINT32 SplitIndexes[5] = { 0 };
    SplitIndexes[0] = sizeof(ETHERNET_HEADER) / 2;
    SplitIndexes[1] = SplitIndexes[0] + sizeof(ETHERNET_HEADER);
    SplitIndexes[2] = SplitIndexes[1] + 1;
    SplitIndexes[3] = SplitIndexes[2] + ((Af == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER));
    SplitIndexes[4] = SplitIndexes[3] + (IsUdp ? sizeof(UDP_HDR) : sizeof(TCP_HDR)) / 2;

    for (UINT16 i = 0; i < RTL_NUMBER_OF(SplitIndexes) - 1; i++) {
        Params.SplitIndexes = &SplitIndexes[i];
        Params.SplitCount = RTL_NUMBER_OF(SplitIndexes) - i;
        Params.IsTxInspect = IsTxInspect;
        Params.LowResources = IsLowResources;
        Params.UseIpNextHeaderMatch = UseIpNextHeaderMatch;
        Params.UseInnerIpPrefixMatch = UseInnerIpPrefixMatch;
        GenericRxFragmentBuffer(Af, &Params);
    }
}

VOID
GenericRxHeaderFragments(
    _In_ ADDRESS_FAMILY Af,
    _In_ XDP_RULE_ACTION ProgramAction,
    _In_ BOOLEAN IsUdp,
    _In_ BOOLEAN IsTxInspect,
    _In_ BOOLEAN IsLowResources,
    _In_ BOOLEAN UseIpNextHeaderMatch,
    _In_ BOOLEAN UseInnerIpPrefixMatch
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.Action = ProgramAction;
    Params.IsUdp = IsUdp;
    Params.PayloadLength = 43;
    Params.Backfill = 13;
    Params.Trailer = 17;
    Params.IsTxInspect = IsTxInspect;
    Params.LowResources = IsLowResources;
    Params.UseIpNextHeaderMatch = UseIpNextHeaderMatch;
    Params.UseInnerIpPrefixMatch = UseInnerIpPrefixMatch;
    UINT16 IpHeaderLength = ((Af == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER));
    if (UseInnerIpPrefixMatch) {
        IpHeaderLength *= 2;
    }
    UINT16 HeadersLength =
        sizeof(ETHERNET_HEADER) +
            ((Af == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER)) +
            IpHeaderLength +
            (IsUdp ? sizeof(UDP_HDR) : sizeof(TCP_HDR));

    GenericRxHeaderMultipleFragments(
        Af, ProgramAction, IsUdp, IsTxInspect, IsLowResources, UseIpNextHeaderMatch, UseInnerIpPrefixMatch);

    for (UINT32 i = 1; i < HeadersLength; i++) {
        Params.SplitIndexes = &i;
        Params.SplitCount = 1;
        GenericRxFragmentBuffer(Af, &Params);
    }

    Params.SplitIndexes = NULL;
    Params.SplitCount = 0;
    GenericRxFragmentBuffer(Af, &Params);
}

VOID
GenericRxFromTxInspect(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMp1QIf;
    UINT16 XskPort;
    SOCKADDR_INET DestAddr = {};

    auto UdpSocket = CreateUdpSocket(Af, &If, &XskPort);
    auto Xsk =
        CreateAndActivateSocket(
            If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_UNSPEC, XSK_BIND_FLAG_NONE,
            &XdpInspectTxL2);

    XskPort = htons(1234);

    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_UDP_DST;
    Rule.Pattern.Port = XskPort;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();

    wil::unique_handle ProgramHandle =
        CreateXdpProg(
            If.GetIfIndex(), &XdpInspectTxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    if (Af == AF_INET) {
        DestAddr.si_family = AF_INET;
        DestAddr.Ipv4.sin_port = XskPort;
        If.GetRemoteIpv4Address(&DestAddr.Ipv4.sin_addr);
    } else {
        DestAddr.si_family = AF_INET6;
        DestAddr.Ipv6.sin6_port = XskPort;
        If.GetRemoteIpv6Address(&DestAddr.Ipv6.sin6_addr);
    }

    //
    // Using UDP segmentation offload, build one send with two UDP frames. The
    // UDP data path should split the send into one NBL chained to two NBs. This
    // feature is only supported on newer versions of Windows; if USO is not
    // supported, send a single frame.
    //
    UINT32 NumFrames;
    UINT32 UdpSegmentSize;
    SIZE_T OptLen = sizeof(UdpSegmentSize);
    CHAR UdpPayload[] = "GenericRxFromTxInspectPkt1GenericRxFromTxInspectPkt2";

    HRESULT Result =
        FnSockGetSockOpt(
            UdpSocket.get(), IPPROTO_UDP, UDP_SEND_MSG_SIZE, (DWORD *)&UdpSegmentSize,
            &OptLen);
    if (FAILED(Result)) {
        TEST_EQUAL(WSAEINVAL, FnSockGetLastError());
        NumFrames = 1;
        UdpSegmentSize = sizeof(UdpPayload) - 1;
    } else {
        NumFrames = 2;
        UdpSegmentSize = (UINT32)(strchr(UdpPayload, '1') - UdpPayload + 1);
        TEST_HRESULT(
            FnSockSetSockOpt(
                UdpSocket.get(), IPPROTO_UDP, UDP_SEND_MSG_SIZE, &UdpSegmentSize,
                sizeof(UdpSegmentSize)));
    }

    TEST_EQUAL((SIZE_T)NumFrames * (SIZE_T)UdpSegmentSize + 1, sizeof(UdpPayload));

    //
    // Produce two XSK fill descriptors, one for each UDP frame.
    //
    SocketProduceRxFill(&Xsk, NumFrames);

    //
    // NDIS restarts protocol stacks from the NIC upwards towards protocols, so
    // XDP completing a bind request does not imply that the TCPIP data path is
    // restarted. Wait until the send succeeds.
    //
    Stopwatch Watchdog(5000);
    INT Bytes;
    do {
        Bytes =
            FnSockSendto(
                UdpSocket.get(), UdpPayload, NumFrames * UdpSegmentSize, FALSE,
                0, (SOCKADDR *)&DestAddr, sizeof(DestAddr));

        if (Bytes != -1) {
            break;
        }
        //
        // TCPIP returns WSAENOBUFS when it cannot reference the data path.
        //
        TEST_EQUAL(WSAENOBUFS, FnSockGetLastError());
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(NumFrames * UdpSegmentSize, (UINT32)Bytes);

    //
    // Verify the NBL propagated correctly to XSK.
    //
    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, NumFrames);
    TEST_EQUAL(NumFrames, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));

    for (UINT32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++) {
        auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex++);

        TEST_EQUAL(UDP_HEADER_BACKFILL(Af) + UdpSegmentSize, RxDesc->Length);
        TEST_TRUE(
            RtlEqualMemory(
                Xsk.Umem.Buffer.get() + UDP_HEADER_BACKFILL(Af) +
                    RxDesc->Address.BaseAddress + RxDesc->Address.Offset,
                &UdpPayload[FrameIndex * UdpSegmentSize],
                UdpSegmentSize));
    }
}

VOID
GenericRxForwardGroHelper(
    _In_ ADDRESS_FAMILY Af,
    _Inout_ GENERIC_RX_FRAGMENT_PARAMS *Params
    )
{
    const auto Booleans = {false, true};
    Params->Action = XDP_PROGRAM_ACTION_L2FWD;

    //
    // Execute the test case over a matrix of NIC configurations.
    //

    for (auto Lso : Booleans) {
    for (auto Checksum : Booleans) {
    for (auto IsLowResources : Booleans) {
        NDIS_OFFLOAD_PARAMETERS OffloadParams;

        InitializeOffloadParameters(&OffloadParams);

        if (Lso) {
            OffloadParams.LsoV2IPv4 = NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED;
            OffloadParams.LsoV2IPv6 = NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED;
        }

        if (Checksum) {
            OffloadParams.IPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
            OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
            OffloadParams.UDPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
            OffloadParams.TCPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
            OffloadParams.TCPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
        }

        Params->LowResources = IsLowResources;

        GenericRxFragmentBuffer(Af, Params);
    }}}
}

VOID
GenericRxForwardGroSanity(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.PayloadLength = 100;
    Params.GroSegCount = Params.PayloadLength;
    GenericRxForwardGroHelper(Af, &Params);
}

VOID
GenericRxForwardGroMdlOffsets(
    _In_ ADDRESS_FAMILY Af
    )
{
    const UINT8 IpHdrSize = Af == AF_INET6 ? sizeof(IPV6_HEADER) : sizeof(IPV4_HEADER);
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.PayloadLength = 60000;
    Params.Backfill = 7;
    Params.Trailer = 23;
    UINT32 SplitIndexes[] = {
        0,
        sizeof(ETHERNET_HEADER) / 2,
        sizeof(ETHERNET_HEADER),
        sizeof(ETHERNET_HEADER) + IpHdrSize / 2,
        sizeof(ETHERNET_HEADER) + IpHdrSize,
        sizeof(ETHERNET_HEADER) + IpHdrSize + sizeof(TCP_HDR) / 2,
        sizeof(ETHERNET_HEADER) + IpHdrSize + sizeof(TCP_HDR),
        sizeof(ETHERNET_HEADER) + IpHdrSize + sizeof(TCP_HDR) + 42,
    };

    for (auto Split : SplitIndexes) {
        if (Split > 0) {
            Params.SplitIndexes = &Split;
            Params.SplitCount = 1;
        } else {
            Params.SplitCount = 0;
        }

        Params.GroSegCount = Params.PayloadLength / 1000;
        GenericRxForwardGroHelper(Af, &Params);
    }
}

VOID
GenericRxForwardGroPureAck(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.GroSegCount = 1;
    GenericRxForwardGroHelper(Af, &Params);
}

VOID
GenericRxForwardGroDataTrailer(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.PayloadLength = 1000;
    Params.GroSegCount = 4;
    Params.DataTrailer = 13;
    GenericRxForwardGroHelper(Af, &Params);
}

VOID
GenericRxForwardGroTcpOptions(
    _In_ ADDRESS_FAMILY Af
    )
{
    UINT8 TcpOptions[TH_MAX_LEN - sizeof(TCP_HDR)];
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.PayloadLength = 1000;
    Params.GroSegCount = 4;
    Params.TcpOptions = TcpOptions;
    Params.TcpOptionsLength = sizeof(TcpOptions);

    for (UINT8 i = 0; i < sizeof(TcpOptions); i++) {
        TcpOptions[i] = 42ui8 + i;
    }

    while (Params.TcpOptionsLength > 0) {
        GenericRxForwardGroHelper(Af, &Params);
        Params.TcpOptionsLength -= sizeof(UINT32);
    }
}

VOID
GenericRxForwardGroMtu(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.PayloadLength = 4000;
    Params.GroSegCount = 1;
    Params.IfMtu = 2000;

    GenericRxForwardGroHelper(Af, &Params);
}

VOID
GenericRxForwardGroMaxOffload(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.PayloadLength = 4000;
    Params.GroSegCount = 1;

    FN_OFFLOAD_OPTIONS OffloadOptions;
    FnMpInitializeOffloadOptions(&OffloadOptions);
    OffloadOptions.GsoMaxOffloadSize = 1000;
    Params.OffloadOptions = &OffloadOptions;

    GenericRxForwardGroHelper(Af, &Params);
}

VOID
GenericRxForwardGroTcpFlags(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_FRAGMENT_PARAMS Params = {0};
    Params.PayloadLength = 4000;
    Params.GroSegCount = 4;

    for (const auto& Fin : {0, TH_FIN}) {
    for (const auto& Psh : {0, TH_PSH}) {
    for (const auto& Ece : {0, TH_ECE}) {
    for (const auto& Cwr : {0, TH_CWR}) {
        Params.TcpFlags = (UINT8)(TH_ACK | Fin | Psh | Ece | Cwr);
        GenericRxForwardGroHelper(Af, &Params);
    }}}}
}

VOID
GenericRxFuzzForwardGro(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMpIf;
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    const UINT16 MaxPayload = (Af == AF_INET6) ? MAX_IPV6_PAYLOAD : MAX_IPV4_PAYLOAD;
    const UINT16 MaxDataTrailer = 100;
    unique_fnmp_task_offload_handle OffloadReset;

    //
    // Set the MTU to the smallest possible value to maximize fuzz efficiency.
    //
    auto MtuReset = MpSetMtuAndWaitForNdis(If, GenericMp, FNMP_MIN_MTU);

    //
    // Forward all packets.
    //
    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_ALL;
    Rule.Action = XDP_PROGRAM_ACTION_L2FWD;
    auto ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    for (UINT32 i = 0; i < 10000; i++) {
        GENERIC_RX_FRAGMENT_PARAMS Params = {0};
        CxPlatVector<UINT32> SplitIndexes;
        UINT8 SplitCount;
        UINT8 TcpOptions[TH_MAX_LEN - sizeof(TCP_HDR)];

        //
        // Periodically advertise new NIC capabilities.
        //
        if ((i % 100) == 0) {
            NDIS_OFFLOAD_PARAMETERS OffloadParams;
            FN_OFFLOAD_OPTIONS OffloadOptions;

            InitializeOffloadParameters(&OffloadParams);
            FnMpInitializeOffloadOptions(&OffloadOptions);

            if (GetRandomUInt32() & 1) {
                OffloadParams.LsoV2IPv4 = NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED;
                OffloadParams.LsoV2IPv6 = NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED;
            }

            if (GetRandomUInt32() & 1) {
                OffloadParams.IPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
                OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
                OffloadParams.UDPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
                OffloadParams.TCPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
                OffloadParams.TCPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
            }

            OffloadOptions.GsoMaxOffloadSize = (GetRandomUInt32() % MaxPayload) + 1;

            //
            // Set the offload and replace the offload resetter without
            // triggering an immediate reset for the old handle (if any) here.
            //
            *OffloadReset.addressof() =
                MpUpdateTaskOffload(
                    GenericMp, FnOffloadCurrentConfig, &OffloadParams, &OffloadOptions).release();
        }

        Params.Action = XDP_PROGRAM_ACTION_L2FWD;
        Params.TcpOptionsLength = (GetRandomUInt32() % (sizeof(TcpOptions) + 1) / 4) * 4;
        Params.PayloadLength =
            (UINT16)(GetRandomUInt32() % (MaxPayload - sizeof(TCP_HDR) - Params.TcpOptionsLength + 1));
        Params.Backfill = GetRandomUInt32() % 100;
        Params.Trailer = GetRandomUInt32() % 100;
        Params.GroSegCount = (GetRandomUInt32() % (Params.PayloadLength + TCP_HEADER_BACKFILL(Af))) + 1;
        Params.LowResources = GetRandomUInt32() & 1;

        if ((GetRandomUInt32() % 4 == 0)) {
            Params.DataTrailer = GetRandomUInt32() % (MaxDataTrailer + 1);
        }

        CxPlatVector<UCHAR> Payload((SIZE_T)Params.PayloadLength + Params.DataTrailer);
        CxPlatRandom(Params.PayloadLength + Params.DataTrailer, Payload.data());

        CxPlatVector<UCHAR> PacketBuffer(
            Params.Backfill + TCP_HEADER_BACKFILL(Af) + sizeof(TcpOptions) +
                Payload.size() + Params.Trailer);
        UINT32 ActualPacketLength =
            (UINT32)PacketBuffer.size() - Params.Backfill - Params.Trailer - Params.DataTrailer;

        CxPlatRandom(Params.TcpOptionsLength, &TcpOptions[0]);

        ETHERNET_ADDRESS DummyEthernet = {0};
        INET_ADDR DummyIp = {0};
        UINT16 DummyPort = 0;
        TEST_TRUE(
            PktBuildTcpFrame(
                &PacketBuffer[0] + Params.Backfill, &ActualPacketLength,
                Payload.empty() ? NULL : &Payload[0], Params.PayloadLength,
                TcpOptions, Params.TcpOptionsLength, 0, 0, TH_ACK, 65535, &DummyEthernet,
                &DummyEthernet, Af, &DummyIp, &DummyIp, DummyPort, DummyPort));

        ActualPacketLength += Params.DataTrailer;

        //
        // Fuzz the headers and the first few bytes of payload.
        //
        for (UINT32 j = 0; j < 100; j++) {
            PacketBuffer[GetRandomUInt32() % std::min(100ui64, PacketBuffer.size() + 1)] =
                (UINT8)GetRandomUInt32();
        }

        if ((GetRandomUInt32() % 10) == 0) {
            ActualPacketLength = (GetRandomUInt32() % ActualPacketLength) + 1;
        }

        //
        // Generate a monotonic increasing sequence of split indexes.
        //
        // The first split can be at index zero only if there is backfill.
        // If there is no backfill, use index one as the minimum.
        //
        UINT8 BackfillOffset = (Params.Backfill > 0) ? 0 : 1;
        SplitCount = (UINT8)std::min(GetRandomUInt32() % 8ui32, ActualPacketLength - BackfillOffset);
        while (SplitCount-- > 0) {
            if (!SplitIndexes.empty()) {
                SplitIndexes.push_back(
                    (GetRandomUInt32() % (ActualPacketLength - SplitCount - SplitIndexes.back() - 1)) +
                        SplitIndexes.back() + 1);
            } else {
                SplitIndexes.push_back(
                    (GetRandomUInt32() % (ActualPacketLength - SplitCount - BackfillOffset)) +
                        BackfillOffset);
            }
        }

        if (!SplitIndexes.empty()) {
            Params.SplitIndexes = &SplitIndexes[0];
            Params.SplitCount = (UINT16)SplitIndexes.size();
        }

        CxPlatVector<DATA_BUFFER> Buffers =
            GenericRxCreateSplitBuffers(
                &PacketBuffer[0], ActualPacketLength, Params.Backfill, Params.Trailer,
                Params.SplitIndexes, Params.SplitCount);

        RX_FRAME Frame;
        RxInitializeFrame(&Frame, If.GetQueueId(), Buffers.data(), (UINT16)Buffers.size());
        Frame.Frame.Input.Rsc.Info.CoalescedSegCount = Params.GroSegCount;
        TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));

        if ((GetRandomUInt32() % 4) == 0) {
            DATA_FLUSH_OPTIONS RxFlushOptions = {0};
            RxFlushOptions.Flags.LowResources = Params.LowResources;
            MpRxFlush(GenericMp, &RxFlushOptions);
        }
    }

    MpRxFlush(GenericMp);
}

static
VOID
GenerateTestPassword(
    _Out_writes_z_(BufferCount) WCHAR *Buffer,
    _In_ UINT32 BufferCount
    )
{
    TEST_TRUE(BufferCount >= 4);
    ASSERT(BufferCount >= 4);

    //
    // Terminate the string and attempt to satisfy complexity requirements with
    // some hard-coded values.
    //
    Buffer[--BufferCount] = L'\0';
    Buffer[--BufferCount] = L'A';
    Buffer[--BufferCount] = L'b';
    Buffer[--BufferCount] = L'#';

    for (UINT32 i = 0; i < BufferCount; i++) {
        static const WCHAR Characters[] =
            L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789";

        unsigned int r;
        CxPlatRandom(sizeof(r), &r);

        Buffer[i] = Characters[r % RTL_NUMBER_OF(Characters)];
    }
}

VOID
SecurityAdjustDeviceAcl()
{
    PCWSTR UserName = L"xdpfnuser";
    WCHAR UserPassword[16 + 1];
    USER_INFO_1 UserInfo = {0};
    NET_API_STATUS UserStatus;
    SE_SID UserSid;
    SID_NAME_USE UserSidUse;
    auto If = FnMpIf;

    //
    // Create a standard, non-admin user on the local system and create a logon
    // session for them.
    //

    UserInfo.usri1_name = (PWSTR)UserName;
    UserInfo.usri1_password = (PWSTR)UserPassword;
    UserInfo.usri1_priv = USER_PRIV_USER;
    UserInfo.usri1_flags = UF_SCRIPT | UF_PASSWD_NOTREQD | UF_DONT_EXPIRE_PASSWD;

    for (UINT32 i = 0; i < 10; i++) {
        GenerateTestPassword(UserPassword, RTL_NUMBER_OF(UserPassword));

        UserStatus = NetUserAdd(NULL, 1, (BYTE *)&UserInfo, NULL);
        if (UserStatus == NERR_Success) {
            break;
        }
    }

    TEST_EQUAL(NERR_Success, UserStatus);

    auto UserRemove = wil::scope_exit([&]
    {
        NET_API_STATUS UserStatus = NetUserDel(NULL, UserName);

        if (UserStatus != NERR_Success) {
            TEST_WARNING("NetUserDel failed: %x", UserStatus);
        }
    });

    wil::unique_handle Token;
    TEST_TRUE(LogonUserW(
        UserName, L".", UserPassword, LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &Token));

    //
    // As the non-admin user, verify XDP denies access to all handle types.
    //
    {
        TEST_TRUE(ImpersonateLoggedOnUser(Token.get()));
        auto Unimpersonate = wil::scope_exit([&]
        {
            RevertToSelf();
        });

        wil::unique_handle Socket;
        TEST_EQUAL(
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),
            TryCreateSocket(Socket));

        wil::unique_handle Interface;
        TEST_EQUAL(
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),
            TryInterfaceOpen(If.GetIfIndex(), Interface));
    }

    //
    // Grant the test's standard user access to XDP.
    //

    DWORD SidSize = sizeof(UserSid);
    WCHAR Domain[256];
    DWORD DomainSize = RTL_NUMBER_OF(Domain);

    TEST_TRUE(LookupAccountNameW(
        NULL, UserName, &UserSid.Sid, &SidSize, Domain, &DomainSize, &UserSidUse));

    wil::unique_hlocal_ansistring SidString;
    TEST_TRUE(ConvertSidToStringSidA(&UserSid.Sid, wil::out_param(SidString)));

    CHAR SddlBuff[256];
    RtlZeroMemory(SddlBuff, sizeof(SddlBuff));
    sprintf_s(SddlBuff, DEFAULT_XDP_SDDL "(A;;GA;;;%s)", SidString.get());

    SetDeviceSddl(SddlBuff);
    auto ResetSddl = wil::scope_exit([&]
    {
        SetDeviceSddl(DEFAULT_XDP_SDDL);

        HRESULT Result = TryRestartService(XDP_SERVICE_NAME);
        if (FAILED(Result)) {
            TEST_WARNING("TryRestartService(XDP_SERVICE_NAME) failed: %x", Result);
        }
    });

    TEST_HRESULT(TryRestartService(XDP_SERVICE_NAME));

    //
    // As the non-admin user, verify XDP now grants access to all handle types.
    //
    {
        TEST_TRUE(ImpersonateLoggedOnUser(Token.get()));
        auto Unimpersonate = wil::scope_exit([&]
        {
            RevertToSelf();
        });

        CreateSocket();
        InterfaceOpen(If.GetIfIndex());
    }
}

VOID
EbpfNetsh()
{
    //
    // Verify XDP metadata is registered with eBPF via store APIs.
    //
    CHAR Path[MAX_PATH];
    CHAR CmdBuff[256] = {0};

    TEST_HRESULT(GetCurrentBinaryPath(Path, RTL_NUMBER_OF(Path)));
    sprintf_s(CmdBuff, "netsh ebpf show verification file=%s\\bpf\\pass.o", Path);
    TEST_EQUAL(0, InvokeSystem(CmdBuff));
}

static
HRESULT
TryAttachEbpfXdpProgram(
    _Out_ unique_bpf_object &BpfObject,
    _In_ const TestInterface &If,
    _In_ const CHAR *BpfRelativeFileName,
    _In_ const CHAR *BpfProgramName,
    _In_ INT AttachFlags = 0
    )
{
    HRESULT Result;
    CHAR BpfAbsoluteFileName[MAX_PATH];
    bpf_program *Program;
    int ProgramFd;
    int ErrnoResult;

    Result = GetCurrentBinaryPath(BpfAbsoluteFileName, RTL_NUMBER_OF(BpfAbsoluteFileName));
    if (FAILED(Result)) {
        goto Exit;
    }

    ErrnoResult = strcat_s(BpfAbsoluteFileName, sizeof(BpfAbsoluteFileName), BpfRelativeFileName);
    if (ErrnoResult != 0) {
        Result = E_FAIL;
        goto Exit;
    }

    BpfObject.reset(bpf_object__open(BpfAbsoluteFileName));
    if (BpfObject.get() == NULL) {
        TraceError("bpf_object__open failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    ErrnoResult = bpf_object__load(BpfObject.get());
    if (ErrnoResult != 0) {
        TraceError("bpf_object__load failed: %d, errno=%d", ErrnoResult, errno);
        Result = E_FAIL;
        goto Exit;
    }

    Program = bpf_object__find_program_by_name(BpfObject.get(), BpfProgramName);
    if (Program == NULL) {
        TraceError("bpf_object__find_program_by_name failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    ProgramFd = bpf_program__fd(Program);
    if (ProgramFd < 0) {
        TraceError("bpf_program__fd failed: %d", errno);
        Result = E_FAIL;
        goto Exit;
    }

    ErrnoResult = bpf_xdp_attach(If.GetIfIndex(), ProgramFd, AttachFlags, NULL);
    if (ErrnoResult != 0) {
        TraceError("bpf_xdp_attach failed: %d, errno=%d", ErrnoResult, errno);
        Result = E_FAIL;
        goto Exit;
    }

    Result = S_OK;

Exit:

    if (FAILED(Result)) {
        BpfObject.reset();
    }

    return Result;
}

static
unique_bpf_object
AttachEbpfXdpProgram(
    _In_ const TestInterface &If,
    _In_ const CHAR *BpfRelativeFileName,
    _In_ const CHAR *BpfProgramName,
    _In_ INT AttachFlags = 0
    )
{
    unique_bpf_object BpfObject;
    HRESULT Result;

    //
    // TODO: https://github.com/microsoft/ebpf-for-windows/issues/2133
    // Workaround till the above issue is fixed (and eBPF returns E_BUSY):
    // Try a few times to load and attach the program with a sleep in between.
    //
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    do {
        Result = TryAttachEbpfXdpProgram(
            BpfObject, If, BpfRelativeFileName, BpfProgramName, AttachFlags);
        if (Result == S_OK) {
            break;
        }
    } while (CxPlatSleep(2 * POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_HRESULT(Result);

    return BpfObject;
}

VOID
GenericRxEbpfAttach()
{
    auto If = FnMpIf;

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\drop.sys", "drop");

    unique_bpf_object BpfObjectReplacement;
    TEST_TRUE(FAILED(TryAttachEbpfXdpProgram(BpfObjectReplacement, If, "\\bpf\\pass.sys", "pass")));

    //
    // eBPF doesn't wait for the pass.sys driver to completely unload after
    // tearing down the object, so allow some time for that to happen before
    // retrying with the replace flag.
    //
    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS);
    BpfObjectReplacement =
        AttachEbpfXdpProgram(If, "\\bpf\\pass.sys", "pass", XDP_FLAGS_REPLACE);
}

VOID
GenericRxEbpfDrop()
{
    auto If = FnMpIf;
    unique_fnmp_handle GenericMp;
    unique_fnlwf_handle FnLwf;
    const UCHAR Payload[] = "GenericRxEbpfDrop";

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\drop.sys", "drop");

    GenericMp = MpOpenGeneric(If.GetIfIndex());
    FnLwf = LwfOpenDefault(If.GetIfIndex());

    CxPlatVector<UCHAR> Mask(sizeof(Payload), 0xFF);
    auto LwfFilter = LwfRxFilter(FnLwf, Payload, Mask.data(), sizeof(Payload));

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), Payload, sizeof(Payload));
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    MpRxFlush(GenericMp);

    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS);

    UINT32 FrameLength = 0;
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        LwfRxGetFrame(FnLwf, If.GetQueueId(), &FrameLength, NULL));
}

VOID
GenericRxEbpfPass()
{
    auto If = FnMpIf;
    unique_fnmp_handle GenericMp;
    unique_fnlwf_handle FnLwf;
    const UCHAR Payload[] = "GenericRxEbpfPass";

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\pass.sys", "pass");

    GenericMp = MpOpenGeneric(If.GetIfIndex());
    FnLwf = LwfOpenDefault(If.GetIfIndex());

    CxPlatVector<UCHAR> Mask(sizeof(Payload), 0xFF);
    auto LwfFilter = LwfRxFilter(FnLwf, Payload, Mask.data(), sizeof(Payload));

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), Payload, sizeof(Payload));
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    MpRxFlush(GenericMp);

    LwfRxAllocateAndGetFrame(FnLwf, If.GetQueueId());
    LwfRxDequeueFrame(FnLwf, If.GetQueueId());
    LwfRxFlush(FnLwf);
}

VOID
GenericRxEbpfTx()
{
    auto If = FnMpIf;
    unique_fnmp_handle GenericMp;
    const UCHAR Payload[] = "GenericRxEbpfTx";

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\l1fwd.sys", "l1fwd");

    GenericMp = MpOpenGeneric(If.GetIfIndex());

    CxPlatVector<UCHAR> Mask(sizeof(Payload), 0xFF);
    auto MpFilter = MpTxFilter(GenericMp, Payload, Mask.data(), sizeof(Payload));

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), Payload, sizeof(Payload));
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    MpRxFlush(GenericMp);

    MpTxAllocateAndGetFrame(GenericMp, If.GetQueueId());
    MpTxDequeueFrame(GenericMp, If.GetQueueId());
    MpTxFlush(GenericMp);
}

VOID
GenericRxEbpfPayload()
{
    auto If = FnMpIf;
    unique_fnmp_handle GenericMp;
    unique_fnlwf_handle FnLwf;
    UINT16 LocalPort = 0, RemotePort = 0;
    ETHERNET_ADDRESS LocalHw = {}, RemoteHw = {};
    INET_ADDR LocalIp = {}, RemoteIp = {};
    const UINT32 Backfill = 13;
    const UINT32 Trailer = 17;
    const UCHAR UdpPayload[] = "GenericRxEbpfPayload";

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\allow_ipv6.sys", "allow_ipv6");

    GenericMp = MpOpenGeneric(If.GetIfIndex());
    FnLwf = LwfOpenDefault(If.GetIfIndex());

    UCHAR UdpFrame[Backfill + UDP_HEADER_STORAGE + sizeof(UdpPayload) + Trailer];
    UINT32 UdpFrameLength = sizeof(UdpFrame) - Backfill - Trailer;
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame + Backfill, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET6, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    CxPlatVector<UCHAR> Mask(UdpFrameLength, 0xFF);
    auto LwfFilter = LwfRxFilter(FnLwf, UdpFrame + Backfill, Mask.data(), UdpFrameLength);

    RX_FRAME Frame;
    DATA_BUFFER Buffer = {0};
    Buffer.DataOffset = Backfill;
    Buffer.DataLength = UdpFrameLength;
    Buffer.BufferLength = Backfill + UdpFrameLength + Trailer;
    Buffer.VirtualAddress = UdpFrame;

    RxInitializeFrame(&Frame, If.GetQueueId(), &Buffer);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    MpRxFlush(GenericMp);

    LwfRxAllocateAndGetFrame(FnLwf, If.GetQueueId());
    LwfRxDequeueFrame(FnLwf, If.GetQueueId());
    LwfRxFlush(FnLwf);
}

VOID
ProgTestRunRxEbpfPayload()
{
    auto If = FnMpIf;
    UINT16 LocalPort = 0, RemotePort = 0;
    ETHERNET_ADDRESS LocalHw = {}, RemoteHw = {};
    INET_ADDR LocalIp = {}, RemoteIp = {};
    const UCHAR UdpPayload[] = "ProgTestRunRxEbpfPayload";
    bpf_test_run_opts Opts = {};

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\allow_ipv6.sys", "allow_ipv6");
    fd_t ProgFd = bpf_program__fd(bpf_object__find_program_by_name(BpfObject.get(), "allow_ipv6"));

    // Build a v6 packet and verify it is allowed.
    UCHAR UdpFrameV6[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UCHAR UdpFrameOutV6[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrameV6);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrameV6, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET6, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    Opts.data_in = UdpFrameV6;
    Opts.data_size_in = sizeof(UdpFrameV6);
    Opts.data_out = UdpFrameOutV6;
    Opts.data_size_out = sizeof(UdpFrameOutV6);
    Opts.ctx_in = nullptr;
    Opts.ctx_size_in = 0;
    Opts.ctx_out = nullptr;
    Opts.ctx_size_out = 0;

    TEST_EQUAL(0, bpf_prog_test_run_opts(ProgFd, &Opts));
    TEST_EQUAL(Opts.retval, XDP_PASS);

    TEST_EQUAL(memcmp(UdpFrameV6, UdpFrameOutV6, sizeof(UdpFrameV6)), 0);

    // Build a v4 packet and verify it is dropped.
    UCHAR UdpFrameV4[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UCHAR UdpFrameOutV4[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UdpFrameLength = sizeof(UdpFrameV4);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrameV4, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    Opts.data_in = UdpFrameV4;
    Opts.data_size_in = sizeof(UdpFrameV4);
    Opts.data_out = UdpFrameOutV4;
    Opts.data_size_out = sizeof(UdpFrameOutV4);
    Opts.ctx_in = nullptr;
    Opts.ctx_size_in = 0;
    Opts.ctx_out = nullptr;
    Opts.ctx_size_out = 0;

    TEST_EQUAL(0, bpf_prog_test_run_opts(ProgFd, &Opts));
    TEST_EQUAL(Opts.retval, XDP_DROP);

    TEST_EQUAL(memcmp(UdpFrameV4, UdpFrameOutV4, sizeof(UdpFrameV4)), 0);
}

VOID
GenericRxEbpfIfIndex()
{
    auto If = FnMpIf;
    unique_fnmp_handle GenericMp;
    unique_fnlwf_handle FnLwf;
    const UCHAR Payload[] = "GenericRxEbpfIfIndex";
    UINT32 Zero = 0;

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\selective_drop.sys", "selective_drop");

    GenericMp = MpOpenGeneric(If.GetIfIndex());
    FnLwf = LwfOpenDefault(If.GetIfIndex());

    // Get the interface_map fd.
    fd_t interface_map_fd = bpf_object__find_map_fd_by_name(BpfObject.get(), "interface_map");
    TEST_NOT_EQUAL(interface_map_fd, ebpf_fd_invalid);

    // Update the interface_map with the interface index of the test interface.
    UINT32 IfIndex = If.GetIfIndex();
    TEST_NOT_EQUAL(IfIndex, IFI_UNSPECIFIED);

    TEST_EQUAL(0, bpf_map_update_elem(interface_map_fd, &Zero, &IfIndex, BPF_ANY));

    CxPlatVector<UCHAR> Mask(sizeof(Payload), 0xFF);
    auto LwfFilter = LwfRxFilter(FnLwf, Payload, Mask.data(), sizeof(Payload));

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), Payload, sizeof(Payload));
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    MpRxFlush(GenericMp);

    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS);

    UINT32 FrameLength = 0;
    // Packet should be dropped.
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        LwfRxGetFrame(FnLwf, If.GetQueueId(), &FrameLength, NULL));

    // Validate that the dropped_packet_map contains a non-0 entry for the IfIndex.
    fd_t dropped_packet_map_fd = bpf_object__find_map_fd_by_name(BpfObject.get(), "dropped_packet_map");
    TEST_NOT_EQUAL(dropped_packet_map_fd, ebpf_fd_invalid);

    UINT64 DroppedPacketCount = 0;
    TEST_EQUAL(0, bpf_map_lookup_elem(dropped_packet_map_fd, &Zero, &DroppedPacketCount));
    TEST_EQUAL(DroppedPacketCount, 1);

    // Now set the ifIndex to some other value and verify that the packet is not dropped.
    UINT32 OtherIfIndex = If.GetIfIndex() + 1;
    TEST_EQUAL(0, bpf_map_update_elem(interface_map_fd, &Zero, &OtherIfIndex, BPF_ANY));

    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    MpRxFlush(GenericMp);

    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS);

    // Packet should not be dropped.
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_MORE_DATA),
        LwfRxGetFrame(FnLwf, If.GetQueueId(), &FrameLength, NULL));
}

VOID
GenericRxEbpfFragments()
{
    auto If = FnMpIf;
    unique_fnmp_handle GenericMp;
    const UINT32 Backfill = 3;
    const UINT32 Trailer = 4;
    const UINT32 SplitAt = 4;
    DATA_BUFFER Buffers[2] = {};
    const UCHAR Payload[] = "123GenericRxEbpfFragments4321";

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\l1fwd.sys", "l1fwd");

    GenericMp = MpOpenGeneric(If.GetIfIndex());

    Buffers[0].DataLength = SplitAt;
    Buffers[0].DataOffset = Backfill;
    Buffers[0].BufferLength = Backfill + SplitAt;
    Buffers[0].VirtualAddress = Payload;
    Buffers[1].DataLength = sizeof(Payload) - Buffers[0].BufferLength - Trailer;
    Buffers[1].DataOffset = 0;
    Buffers[1].BufferLength = Buffers[1].DataLength + Trailer;
    Buffers[1].VirtualAddress = Payload + Buffers[0].BufferLength;

    //
    // XDP-for-Windows has limited eBPF support for fragments: the first buffer
    // is visible to eBPF programs, and the remaining fragments (if any) are
    // inaccessible.
    //
    // Actions apply to the entire frame, not just to the first fragement.
    //
    UINT32 MaskSize = Buffers[0].DataLength + Buffers[1].DataLength;
    CxPlatVector<UCHAR> Mask(MaskSize, 0xFF);
    auto MpFilter = MpTxFilter(GenericMp, Payload + Backfill, Mask.data(), MaskSize);

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, If.GetQueueId(), Buffers, RTL_NUMBER_OF(Buffers));
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    MpRxFlush(GenericMp);

    MpTxAllocateAndGetFrame(GenericMp, If.GetQueueId());
    MpTxDequeueFrame(GenericMp, If.GetQueueId());
    MpTxFlush(GenericMp);
}

VOID
GenericRxEbpfUnload()
{
    auto If = FnMpIf;

    unique_bpf_object BpfObject = AttachEbpfXdpProgram(If, "\\bpf\\pass.sys", "pass");

    TEST_HRESULT(TryStopService(XDP_SERVICE_NAME));
    TEST_HRESULT(TryStartService(XDP_SERVICE_NAME));
}

VOID
GenericTxToRxInject()
{
    auto If = FnMpIf;
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    XDP_HOOK_ID TxInjectToRxL2 = {};

    TxInjectToRxL2.Layer = XDP_HOOK_L2;
    TxInjectToRxL2.Direction = XDP_HOOK_RX;
    TxInjectToRxL2.SubLayer = XDP_HOOK_INJECT;

    auto UdpSocket = CreateUdpSocket(AF_INET, &If, &LocalPort);
    auto Xsk =
        CreateAndActivateSocket(
            If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_UNSPEC, XSK_BIND_FLAG_NONE, nullptr,
            &TxInjectToRxL2);

    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv4Address(&LocalIp.Ipv4);
    If.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UINT16 FrameOffset = 16;
    UCHAR UdpPayload[] = "GenericTxToRxInject";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *UdpFrame = Xsk.Umem.Buffer.get() + TxBuffer + FrameOffset;
    UINT32 UdpFrameLength = Xsk.Umem.Reg.ChunkSize - FrameOffset;
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->Address.BaseAddress = TxBuffer;
    TxDesc->Address.Offset = FrameOffset;
    TxDesc->Length = UdpFrameLength;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
}

VOID
GenericTxSingleFrame()
{
    auto If = FnMpIf;
    auto Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    UINT64 Pattern = 0xA5CC7729CE99C16Aui64;
    UINT64 Mask = ~0ui64;

    auto MpFilter = MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    UINT16 FrameOffset = 13;
    UCHAR Payload[] = "GenericTxSingleFrame";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer + FrameOffset;
    UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
    ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.ChunkSize);

    RtlCopyMemory(TxFrame, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame + sizeof(Pattern), Payload, sizeof(Payload));

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->Address.BaseAddress = TxBuffer;
    TxDesc->Address.Offset = FrameOffset;
    TxDesc->Length = TxFrameLength;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
    TEST_EQUAL(1, MpTxFrame->BufferCount);

    const DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[0];
    TEST_EQUAL(TxFrameLength, MpTxBuffer->BufferLength);
#pragma warning(push)
#pragma warning(disable:6385)  // Reading invalid data from 'TxFrame':  the readable size is '_Old_10`8' bytes, but '29' bytes may be read.
    TEST_TRUE(
        RtlEqualMemory(
            TxFrame, MpTxBuffer->VirtualAddress + MpTxBuffer->DataOffset,
            TxFrameLength));
#pragma warning(pop)

    MpTxDequeueFrame(GenericMp, 0);
    MpTxFlush(GenericMp);

    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Completion, 1);
    TEST_EQUAL(TxBuffer, SocketGetTxCompDesc(&Xsk, ConsumerIndex));
}

VOID
GenericTxOutOfOrder()
{
    auto If = FnMpIf;
    auto Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    UINT64 Pattern = 0x2865A18EE4DB02F0ui64;
    UINT64 Mask = ~0ui64;

    auto MpFilter = MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    UINT16 FrameOffset = 13;
    UCHAR Payload[] = "GenericTxOutOfOrder";
    UINT64 TxBuffer0 = SocketFreePop(&Xsk);
    UINT64 TxBuffer1 = SocketFreePop(&Xsk);
    UCHAR *TxFrame0 = Xsk.Umem.Buffer.get() + TxBuffer0 + FrameOffset;
    UCHAR *TxFrame1 = Xsk.Umem.Buffer.get() + TxBuffer1 + FrameOffset;
    UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
    ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.ChunkSize);

    RtlCopyMemory(TxFrame0, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame1, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame0 + sizeof(Pattern), Payload, sizeof(Payload));
    RtlCopyMemory(TxFrame1 + sizeof(Pattern), Payload, sizeof(Payload));

    UINT32 ProducerIndex;
    TEST_EQUAL(2, XskRingProducerReserve(&Xsk.Rings.Tx, 2, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc0 = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc0->Address.BaseAddress = TxBuffer0;
    TxDesc0->Address.Offset = FrameOffset;
    TxDesc0->Length = TxFrameLength;
    XSK_BUFFER_DESCRIPTOR *TxDesc1 = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc1->Address.BaseAddress = TxBuffer1;
    TxDesc1->Address.Offset = FrameOffset;
    TxDesc1->Length = TxFrameLength;

    XskRingProducerSubmit(&Xsk.Rings.Tx, 2);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    MpTxDequeueFrame(GenericMp, 1);
    MpTxFlush(GenericMp);

    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Completion, 1);
    TEST_EQUAL(TxBuffer1, SocketGetTxCompDesc(&Xsk, ConsumerIndex));
    XskRingConsumerRelease(&Xsk.Rings.Completion, 1);

    MpTxDequeueFrame(GenericMp, 0);
    MpTxFlush(GenericMp);

    ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Completion, 1);
    TEST_EQUAL(TxBuffer0, SocketGetTxCompDesc(&Xsk, ConsumerIndex));
}

VOID
GenericTxSharing()
{
    auto If = FnMpIf;
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    MY_SOCKET Sockets[4] = {};

    for (UINT32 i = 0; i < RTL_NUMBER_OF(Sockets); i++) {
        Sockets[i] =
            CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    }

    for (UINT32 i = 0; i < RTL_NUMBER_OF(Sockets); i++) {
        auto &Xsk = Sockets[i];
        UINT64 Pattern = 0xA5CC7729CE99C16Aui64 + i;
        UINT64 Mask = ~0ui64;

        auto MpFilter = MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

        UINT16 FrameOffset = 13;
        UCHAR Payload[] = "GenericTxSharing";
        UINT64 TxBuffer = SocketFreePop(&Xsk);
        UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer + FrameOffset;
        UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
        ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.ChunkSize);

        RtlCopyMemory(TxFrame, &Pattern, sizeof(Pattern));
        RtlCopyMemory(TxFrame + sizeof(Pattern), Payload, sizeof(Payload));

        UINT32 ProducerIndex;
        TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

        XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
        TxDesc->Address.BaseAddress = TxBuffer;
        TxDesc->Address.Offset = FrameOffset;
        TxDesc->Length = TxFrameLength;
        XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

        XSK_NOTIFY_RESULT_FLAGS NotifyResult;
        NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
        TEST_EQUAL(0, NotifyResult);

        auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
        TEST_EQUAL(1, MpTxFrame->BufferCount);

        const DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[0];
        TEST_EQUAL(TxFrameLength, MpTxBuffer->BufferLength);
#pragma warning(push)
#pragma warning(disable:6385)  // Reading invalid data from 'TxFrame':  the readable size is '_Old_10`8' bytes, but '29' bytes may be read.
        TEST_TRUE(
            RtlEqualMemory(
                TxFrame, MpTxBuffer->VirtualAddress + MpTxBuffer->DataOffset,
                TxFrameLength));
#pragma warning(pop)

        MpTxDequeueFrame(GenericMp, 0);
        MpTxFlush(GenericMp);

        UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Completion, 1);
        TEST_EQUAL(TxBuffer, SocketGetTxCompDesc(&Xsk, ConsumerIndex));
    }
}

VOID
GenericTxPoke()
{
    auto If = FnMpIf;
    auto Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    UINT64 Pattern = 0x4FA3DF603CC44911ui64;
    UINT64 Mask = ~0ui64;

    auto MpFilter = MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    UINT16 FrameOffset = 13;
    UCHAR Payload[] = "GenericTxPoke";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer + FrameOffset;
    UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
    ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.ChunkSize);

    RtlCopyMemory(TxFrame, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame + sizeof(Pattern), Payload, sizeof(Payload));

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->Address.BaseAddress = TxBuffer;
    TxDesc->Address.Offset = FrameOffset;
    TxDesc->Length = TxFrameLength;

    TEST_TRUE(XskRingProducerNeedPoke(&Xsk.Rings.Tx));
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    //
    // While NBLs are pending in the miniport, XSKs should not require TX pokes.
    //
    MpTxAllocateAndGetFrame(GenericMp, 0);
    TEST_FALSE(XskRingProducerNeedPoke(&Xsk.Rings.Tx));

    MpTxDequeueFrame(GenericMp, 0);
    MpTxFlush(GenericMp);

    //
    // XSKs require TX pokes when no sends are outstanding.
    //
    SocketConsumerReserve(&Xsk.Rings.Completion, 1);
    SocketProducerCheckNeedPoke(&Xsk.Rings.Tx, TRUE);
}

VOID
GenericTxMtu()
{
    auto If = FnMpIf;
    auto Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    const UINT32 TestMtu = 2048;
    C_ASSERT(TestMtu != FNMP_DEFAULT_MTU);
    C_ASSERT(TestMtu < DEFAULT_UMEM_CHUNK_SIZE);

    auto MtuReset = MpSetMtu(If, GenericMp, TestMtu);

    //
    // The XSK TX path should be torn down after an MTU change.
    //
    Stopwatch Watchdog(MP_RESTART_TIMEOUT_MS);
    do {
        if (XskRingError(&Xsk.Rings.Tx)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_TRUE(XskRingError(&Xsk.Rings.Tx));

    //
    // Wait for the MTU changes to quiesce. If filter or protocol drivers
    // incompatible with MTU changes are installed, this requires a complete
    // detach/attach cycle of the entire interface stack.
    //
    WaitForNdisDatapath(If, MP_RESTART_TIMEOUT_MS);

    Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);

    //
    // Post a TX of size MTU and verify the packet was completed.
    //

    UCHAR Payload[] = "GenericTxMtu";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
    RtlCopyMemory(TxFrame, Payload, sizeof(Payload));

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->Address.AddressAndOffset = TxBuffer;
    TxDesc->Length = TestMtu;

    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);
    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);
    SocketConsumerReserve(&Xsk.Rings.Completion, 1);

    XSK_STATISTICS Stats = {0};
    UINT32 StatsSize = sizeof(Stats);
    GetSockopt(Xsk.Handle.get(), XSK_SOCKOPT_STATISTICS, &Stats, &StatsSize);
    TEST_EQUAL(0, Stats.TxInvalidDescriptors);

    //
    // Post a TX larger than the MTU and verify the packet was dropped.
    //

    TxBuffer = SocketFreePop(&Xsk);
    TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
    RtlCopyMemory(TxFrame, Payload, sizeof(Payload));

    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->Address.AddressAndOffset = TxBuffer;
    TxDesc->Length = TestMtu + 1;

    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    Watchdog.Reset();
    do {
        GetSockopt(Xsk.Handle.get(), XSK_SOCKOPT_STATISTICS, &Stats, &StatsSize);

        if (Stats.TxInvalidDescriptors == 1) {
            break;
        }
    } while (!Watchdog.IsExpired());
    TEST_EQUAL(1, Stats.TxInvalidDescriptors);
}

VOID
GenericTxChecksumOffloadExtensions()
{
    auto If = FnMpIf;
    const BOOLEAN Rx = FALSE, Tx = TRUE;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);

    UINT16 LayoutExtension;
    UINT16 ChecksumExtension;
    UINT32 OptionLength;

    OptionLength = sizeof(LayoutExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION, &LayoutExtension,
            &OptionLength));

    OptionLength = sizeof(ChecksumExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION, &LayoutExtension,
            &OptionLength));

    UINT32 Enabled = TRUE;
    SetSockopt(Xsk.Handle.get(), XSK_SOCKOPT_TX_OFFLOAD_CHECKSUM, &Enabled, sizeof(Enabled));

    OptionLength = sizeof(LayoutExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION, &LayoutExtension,
            &OptionLength));

    OptionLength = sizeof(ChecksumExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION, &LayoutExtension,
            &OptionLength));

    ActivateSocket(&Xsk, Rx, Tx);

    OptionLength = sizeof(LayoutExtension);
    GetSockopt(
        Xsk.Handle.get(), XSK_SOCKOPT_TX_FRAME_LAYOUT_EXTENSION, &LayoutExtension,
        &OptionLength);
    TEST_TRUE(LayoutExtension >= sizeof(XSK_FRAME_DESCRIPTOR));

    OptionLength = sizeof(ChecksumExtension);
    GetSockopt(
        Xsk.Handle.get(), XSK_SOCKOPT_TX_FRAME_CHECKSUM_EXTENSION, &ChecksumExtension,
        &OptionLength);
    TEST_TRUE(ChecksumExtension >= sizeof(XSK_FRAME_DESCRIPTOR));

    TEST_NOT_EQUAL(LayoutExtension, ChecksumExtension);
    TEST_TRUE(
        Xsk.Rings.Tx.ElementStride >=
            sizeof(XSK_FRAME_DESCRIPTOR) + sizeof(XDP_FRAME_LAYOUT) + sizeof(XDP_FRAME_CHECKSUM));
}

struct RxOffloadPreferences {
    ULONG TcpChecksumSucceeded;
    ULONG TcpChecksumFailed;
    ULONG UdpChecksumSucceeded;
    ULONG UdpChecksumFailed;
    ULONG IpChecksumSucceeded;
    ULONG IpChecksumFailed;
};

MY_SOCKET RxChecksumTestHelper(
    _In_ RxOffloadPreferences *TestOffload,
    _In_ BOOLEAN TestRebind,
    _In_ BOOLEAN TestTcp,
    _In_ ADDRESS_FAMILY Af
) {
    const BOOLEAN Rx = TRUE, Tx = FALSE;
    auto If = FnMpIf;
    if (TestTcp) {
        If = FnMp1QIf;
    }

    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);

    //
    // Create an auxiliary socket to reserve a port so that we don't interfere with other
    // processes.
    //
    RemotePort = htons(4321);
    if (TestTcp) {
        UINT32 AckNum = 0;
        auto AuxTcpSocket = CreateTcpSocket(Af, &If, &LocalPort, RemotePort, &AckNum);
    } else {
        auto AuxUdpSocket = CreateUdpSocket(Af, NULL, &LocalPort);
    }

    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    EnableRxChecksumOffload(&Xsk);
    ActivateSocket(&Xsk, Rx, Tx);

    if (TestRebind) {
        Xsk.RxProgram =
            SocketAttachRxProgram(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Xsk.Handle.get());
        Xsk.RxProgram.reset();
        Xsk.RxProgram =
            SocketAttachRxProgram(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Xsk.Handle.get());
    } else {
        Xsk.RxProgram =
            SocketAttachRxProgram(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Xsk.Handle.get());
    }

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv4Address(&LocalIp.Ipv4);
    If.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    RX_FRAME RxFrame;
    if (TestTcp) {
        // Construct a valid IPv4/IPv6 + TCP frame with a valid checksum
        UCHAR TcpPayload[] = "GenericRxChecksumOffloadTcp";
        UCHAR TcpFrame[TCP_HEADER_STORAGE + sizeof(TcpPayload)];
        UINT32 TcpFrameLength = sizeof(TcpFrame);
        TEST_TRUE(
            PktBuildTcpFrame(
                TcpFrame, &TcpFrameLength, TcpPayload, sizeof(TcpPayload), NULL, 0, 0, 0, TH_SYN, 0,
                &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        RxInitializeFrame(&RxFrame, If.GetQueueId(), TcpFrame, TcpFrameLength);
    } else {
        // Construct a valid IPv4/IPv6 + UDP frame with a valid checksum
        UCHAR UdpPayload[] = "GenericRxChecksumOffloadUdp";
        UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
        UINT32 UdpFrameLength = sizeof(UdpFrame);
        TEST_TRUE(
            PktBuildUdpFrame(
                UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
                &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));
        RxInitializeFrame(&RxFrame, If.GetQueueId(), UdpFrame, UdpFrameLength);
    }


    RxFrame.Frame.Input.Checksum.Receive.TcpChecksumSucceeded = TestOffload->TcpChecksumSucceeded;
    RxFrame.Frame.Input.Checksum.Receive.TcpChecksumFailed = TestOffload->TcpChecksumFailed;
    RxFrame.Frame.Input.Checksum.Receive.UdpChecksumSucceeded = TestOffload->UdpChecksumSucceeded;
    RxFrame.Frame.Input.Checksum.Receive.UdpChecksumFailed = TestOffload->UdpChecksumFailed;
    RxFrame.Frame.Input.Checksum.Receive.IpChecksumSucceeded = TestOffload->IpChecksumSucceeded;
    RxFrame.Frame.Input.Checksum.Receive.IpChecksumFailed = TestOffload->IpChecksumFailed;

    // Inject the frame as if it came from the wire
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &RxFrame));

    //
    // Produce one XSK fill descriptor.
    //
    SocketProduceRxFill(&Xsk, 1);

    // Flush RX path to make frame visible to the socket
    MpRxFlush(GenericMp);

    return Xsk;
}

VOID
GenericRxChecksumOffloadExtensions() {
    auto If = FnMpIf;
    const BOOLEAN Rx = TRUE, Tx = FALSE;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);

    UINT16 LayoutExtension;
    UINT16 ChecksumExtension;
    UINT32 OptionLength;

    //
    // Routine Description:
    //     Verify that the RX checksum offload extensions are present when
    //     checksum offload is enabled.
    //

    OptionLength = sizeof(LayoutExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_RX_FRAME_LAYOUT_EXTENSION, &LayoutExtension,
            &OptionLength));

    OptionLength = sizeof(ChecksumExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_RX_FRAME_CHECKSUM_EXTENSION, &LayoutExtension,
            &OptionLength));

    UINT32 Enabled = TRUE;
    SetSockopt(Xsk.Handle.get(), XSK_SOCKOPT_RX_OFFLOAD_CHECKSUM, &Enabled, sizeof(Enabled));

    OptionLength = sizeof(LayoutExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_RX_FRAME_LAYOUT_EXTENSION, &LayoutExtension,
            &OptionLength));

    OptionLength = sizeof(ChecksumExtension);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_RX_FRAME_CHECKSUM_EXTENSION, &LayoutExtension,
            &OptionLength));

    ActivateSocket(&Xsk, Rx, Tx);

    OptionLength = sizeof(LayoutExtension);
    GetSockopt(
        Xsk.Handle.get(), XSK_SOCKOPT_RX_FRAME_LAYOUT_EXTENSION, &LayoutExtension,
        &OptionLength);
    TEST_TRUE(LayoutExtension >= sizeof(XSK_FRAME_DESCRIPTOR));

    OptionLength = sizeof(ChecksumExtension);
    GetSockopt(
        Xsk.Handle.get(), XSK_SOCKOPT_RX_FRAME_CHECKSUM_EXTENSION, &ChecksumExtension,
        &OptionLength);
    TEST_TRUE(ChecksumExtension >= sizeof(XSK_FRAME_DESCRIPTOR));

    TEST_NOT_EQUAL(LayoutExtension, ChecksumExtension);
    TEST_TRUE(
        Xsk.Rings.Rx.ElementStride >=
            sizeof(XSK_FRAME_DESCRIPTOR) + sizeof(XDP_FRAME_LAYOUT) + sizeof(XDP_FRAME_CHECKSUM));
}

VOID
GenericTxChecksumOffloadIp()
{
    const BOOLEAN Rx = FALSE, Tx = TRUE;
    auto If = FnMpIf;
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv4Address(&LocalIp.Ipv4);
    If.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    EnableTxChecksumOffload(&Xsk);
    ActivateSocket(&Xsk, Rx, Tx);

    UCHAR UdpPayload[] = "GenericTxChecksumOffloadIp";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
    UINT32 UdpFrameLength = Xsk.Umem.Reg.ChunkSize;
    TEST_TRUE(
        PktBuildUdpFrame(
            TxFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    CxPlatVector<UCHAR> Mask(UdpFrameLength, 0xFF);
    auto MpFilter = MpTxFilter(GenericMp, TxFrame, &Mask, UdpFrameLength);

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));
    XSK_FRAME_DESCRIPTOR *TxDesc = SocketGetTxFrameDesc(&Xsk, ProducerIndex++);
    TxDesc->Buffer.Address.AddressAndOffset = TxBuffer;
    TxDesc->Buffer.Length = UdpFrameLength;
    XDP_FRAME_LAYOUT *Layout =
        (XDP_FRAME_LAYOUT *)RTL_PTR_ADD(TxDesc, Xsk.Extensions.TxFrameLayoutExtension);
    Layout->Layer2Type = XdpFrameLayer2TypeEthernet;
    Layout->Layer2HeaderLength = sizeof(ETHERNET_HEADER);
    Layout->Layer3Type = XdpFrameLayer3TypeIPv4NoOptions;
    Layout->Layer3HeaderLength = sizeof(IPV4_HEADER);
    Layout->Layer4Type = XdpFrameLayer4TypeUnspecified;
    Layout->Layer4HeaderLength = 0;
    XDP_FRAME_CHECKSUM *Checksum =
        (XDP_FRAME_CHECKSUM *)RTL_PTR_ADD(TxDesc, Xsk.Extensions.TxFrameChecksumExtension);
    Checksum->Layer3 = XdpFrameTxChecksumActionRequired;
    Checksum->Layer4 = XdpFrameTxChecksumActionPassthrough;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, If.GetQueueId());

    TEST_EQUAL(TRUE, MpTxFrame->Output.Checksum.Transmit.IpHeaderChecksum);
    TEST_EQUAL(TRUE, MpTxFrame->Output.Checksum.Transmit.IsIPv4);
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.TcpChecksum);
    TEST_EQUAL(0, MpTxFrame->Output.Checksum.Transmit.TcpHeaderOffset);
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.UdpChecksum);

    MpTxDequeueFrame(GenericMp, If.GetQueueId());
    MpTxFlush(GenericMp);
}

VOID
GenericRxChecksumOffloadIp(BOOLEAN TestRebind) {

    //
    // Routine Description:
    //     This test verifies the metadata that indicates RX checksum offloads were done for IP.
    //     It does so by injecting a UDP frame with a valid IP checksum and verifying that
    //     the metadata is correctly set by the miniport driver.
    //

    RxOffloadPreferences TestOffload = {0};
    TestOffload.IpChecksumSucceeded = TRUE;
    auto Xsk = RxChecksumTestHelper(
        &TestOffload, TestRebind, FALSE, AF_INET);

    UINT32 ConsumerIndex;
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, 1, &ConsumerIndex));

    XSK_FRAME_DESCRIPTOR *RxDesc = SocketGetRxFrameDesc(&Xsk, ConsumerIndex++);
    TEST_TRUE(Xsk.Extensions.RxFrameChecksumExtension != 0);
    TEST_TRUE(Xsk.Extensions.RxFrameLayoutExtension != 0);

    // Get and validate checksum metadata
    XDP_FRAME_CHECKSUM *Checksum =
        (XDP_FRAME_CHECKSUM *)RTL_PTR_ADD(RxDesc, Xsk.Extensions.RxFrameChecksumExtension);
    XDP_FRAME_LAYOUT *Layout =
        (XDP_FRAME_LAYOUT *)RTL_PTR_ADD(RxDesc, Xsk.Extensions.RxFrameLayoutExtension);

    TEST_EQUAL(XdpFrameRxChecksumEvaluationSucceeded, Checksum->Layer3);
    TEST_EQUAL(XdpFrameRxChecksumEvaluationNotChecked, Checksum->Layer4);
    TEST_EQUAL(XdpFrameLayer4TypeUnspecified, Layout->Layer4Type);

    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);
}


VOID
GenericTxChecksumOffloadTcp(
    ADDRESS_FAMILY Af
    )
{
    const BOOLEAN Rx = FALSE, Tx = TRUE;
    auto If = FnMp1QIf;
    UINT16 LocalPort;
    UINT16 RemotePort = htons(1234);
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    UINT32 AckNum;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);
    auto TcpSocket = CreateTcpSocket(Af, &If, &LocalPort, RemotePort, &AckNum);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    EnableTxChecksumOffload(&Xsk);
    ActivateSocket(&Xsk, Rx, Tx);

    UCHAR TcpPayload[] = "GenericTxChecksumOffloadTcp";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
    UINT32 TcpFrameLength = Xsk.Umem.Reg.ChunkSize;
    TEST_TRUE(
        PktBuildTcpFrame(
            TxFrame, &TcpFrameLength, TcpPayload, sizeof(TcpPayload), NULL, 0, 0, AckNum, TH_SYN, 0,
            &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    CxPlatVector<UCHAR> Mask(TcpFrameLength, 0xFF);
    auto MpFilter = MpTxFilter(GenericMp, TxFrame, &Mask, TcpFrameLength);

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_FRAME_DESCRIPTOR *TxDesc = SocketGetTxFrameDesc(&Xsk, ProducerIndex++);
    TxDesc->Buffer.Address.AddressAndOffset = TxBuffer;
    TxDesc->Buffer.Length = TcpFrameLength;
    XDP_FRAME_LAYOUT *Layout =
        (XDP_FRAME_LAYOUT *)RTL_PTR_ADD(TxDesc, Xsk.Extensions.TxFrameLayoutExtension);
    Layout->Layer2Type = XdpFrameLayer2TypeEthernet;
    Layout->Layer2HeaderLength = sizeof(ETHERNET_HEADER);
    Layout->Layer3Type =
        Af == AF_INET6 ?
            XdpFrameLayer3TypeIPv6NoExtensions : XdpFrameLayer3TypeIPv4UnspecifiedOptions;
    Layout->Layer3HeaderLength = Af == AF_INET6 ? sizeof(IPV6_HEADER) : sizeof(IPV4_HEADER);
    Layout->Layer4Type = XdpFrameLayer4TypeTcp;
    Layout->Layer4HeaderLength = sizeof(TCP_HDR);
    XDP_FRAME_CHECKSUM *Checksum =
        (XDP_FRAME_CHECKSUM *)RTL_PTR_ADD(TxDesc, Xsk.Extensions.TxFrameChecksumExtension);
    Checksum->Layer3 = XdpFrameTxChecksumActionPassthrough;
    Checksum->Layer4 = XdpFrameTxChecksumActionRequired;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, If.GetQueueId());

    TEST_EQUAL(TRUE, MpTxFrame->Output.Checksum.Transmit.TcpChecksum);
    TEST_EQUAL(
        (UINT32)(Layout->Layer2HeaderLength + Layout->Layer3HeaderLength),
        MpTxFrame->Output.Checksum.Transmit.TcpHeaderOffset);
    TEST_EQUAL((ULONG)(Af == AF_INET), MpTxFrame->Output.Checksum.Transmit.IsIPv4);
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.IpHeaderChecksum);
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.UdpChecksum);

    MpTxDequeueFrame(GenericMp, If.GetQueueId());
    MpTxFlush(GenericMp);
}

VOID
GenericRxChecksumOffloadTcp(
    ADDRESS_FAMILY Af
    )
{

    //
    // Routine Description:
    //     This test verifies the metadata that indicates RX checksum offloads were done for TCP.
    //     It does so by injecting a TCP frame with a valid TCP checksum and verifying that
    //     the metadata is correctly set by the miniport driver.
    //

    RxOffloadPreferences TestOffload = {0};
    TestOffload.TcpChecksumSucceeded = TRUE;
    auto Xsk = RxChecksumTestHelper(
        &TestOffload, FALSE, TRUE, Af);

    UINT32 ConsumerIndex;
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, 1, &ConsumerIndex));

    XSK_FRAME_DESCRIPTOR *RxDesc = SocketGetRxFrameDesc(&Xsk, ConsumerIndex++);
    TEST_TRUE(Xsk.Extensions.RxFrameChecksumExtension != 0);
    TEST_TRUE(Xsk.Extensions.RxFrameLayoutExtension != 0);

    // Get and validate checksum metadata
    XDP_FRAME_CHECKSUM *Checksum =
        (XDP_FRAME_CHECKSUM *)RTL_PTR_ADD(RxDesc, Xsk.Extensions.RxFrameChecksumExtension);
    XDP_FRAME_LAYOUT *Layout =
        (XDP_FRAME_LAYOUT *)RTL_PTR_ADD(RxDesc, Xsk.Extensions.RxFrameLayoutExtension);

    TEST_EQUAL(XdpFrameRxChecksumEvaluationNotChecked, Checksum->Layer3);
    TEST_EQUAL(XdpFrameRxChecksumEvaluationSucceeded, Checksum->Layer4);
    TEST_EQUAL(XdpFrameLayer4TypeTcp, Layout->Layer4Type);

    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);
}

VOID
GenericTxChecksumOffloadUdp(
    ADDRESS_FAMILY Af
    )
{
    const BOOLEAN Rx = FALSE, Tx = TRUE;
    auto If = FnMpIf;
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);
    auto UdpSocket = CreateUdpSocket(Af, NULL, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
    }

    EnableTxChecksumOffload(&Xsk);
    ActivateSocket(&Xsk, Rx, Tx);

    UCHAR UdpPayload[] = "GenericTxChecksumOffloadUdp";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
    UINT32 UdpFrameLength = Xsk.Umem.Reg.ChunkSize;
    TEST_TRUE(
        PktBuildUdpFrame(
            TxFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    CxPlatVector<UCHAR> Mask(UdpFrameLength, 0xFF);
    auto MpFilter = MpTxFilter(GenericMp, TxFrame, &Mask, UdpFrameLength);

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_FRAME_DESCRIPTOR *TxDesc = SocketGetTxFrameDesc(&Xsk, ProducerIndex++);
    TxDesc->Buffer.Address.AddressAndOffset = TxBuffer;
    TxDesc->Buffer.Length = UdpFrameLength;
    XDP_FRAME_LAYOUT *Layout =
        (XDP_FRAME_LAYOUT *)RTL_PTR_ADD(TxDesc, Xsk.Extensions.TxFrameLayoutExtension);
    Layout->Layer2Type = XdpFrameLayer2TypeEthernet;
    Layout->Layer2HeaderLength = sizeof(ETHERNET_HEADER);
    Layout->Layer3Type =
        Af == AF_INET6 ?
            XdpFrameLayer3TypeIPv6NoExtensions : XdpFrameLayer3TypeIPv4UnspecifiedOptions;
    Layout->Layer3HeaderLength = Af == AF_INET6 ? sizeof(IPV6_HEADER) : sizeof(IPV4_HEADER);
    Layout->Layer4Type = XdpFrameLayer4TypeUdp;
    Layout->Layer4HeaderLength = sizeof(UDP_HDR);
    XDP_FRAME_CHECKSUM *Checksum =
        (XDP_FRAME_CHECKSUM *)RTL_PTR_ADD(TxDesc, Xsk.Extensions.TxFrameChecksumExtension);
    Checksum->Layer3 = XdpFrameTxChecksumActionPassthrough;
    Checksum->Layer4 = XdpFrameTxChecksumActionRequired;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, If.GetQueueId());

    TEST_EQUAL(TRUE, MpTxFrame->Output.Checksum.Transmit.UdpChecksum);
    TEST_EQUAL((ULONG)(Af == AF_INET), MpTxFrame->Output.Checksum.Transmit.IsIPv4);
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.IpHeaderChecksum);
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.TcpChecksum);
    TEST_EQUAL(0, MpTxFrame->Output.Checksum.Transmit.TcpHeaderOffset);

    MpTxDequeueFrame(GenericMp, If.GetQueueId());
    MpTxFlush(GenericMp);
}

VOID
GenericRxChecksumOffloadUdp(
    ADDRESS_FAMILY Af
    )
{

    //
    // Routine Description:
    //     This test verifies the metadata that indicates RX checksum offloads were done for UDP.
    //     It does so by injecting a UDP frame with a valid UDP checksum and verifying that
    //     the metadata is correctly set by the miniport driver.
    //

    RxOffloadPreferences TestOffload = {0};
    TestOffload.UdpChecksumSucceeded = TRUE;
    auto Xsk = RxChecksumTestHelper(
        &TestOffload, FALSE, FALSE, Af);

    UINT32 ConsumerIndex;
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, 1, &ConsumerIndex));

    XSK_FRAME_DESCRIPTOR *RxDesc = SocketGetRxFrameDesc(&Xsk, ConsumerIndex++);
    TEST_TRUE(Xsk.Extensions.RxFrameChecksumExtension != 0);
    TEST_TRUE(Xsk.Extensions.RxFrameLayoutExtension != 0);

    // Get and validate checksum metadata
    XDP_FRAME_CHECKSUM *Checksum =
        (XDP_FRAME_CHECKSUM *)RTL_PTR_ADD(RxDesc, Xsk.Extensions.RxFrameChecksumExtension);
    XDP_FRAME_LAYOUT *Layout =
        (XDP_FRAME_LAYOUT *)RTL_PTR_ADD(RxDesc, Xsk.Extensions.RxFrameLayoutExtension);

    TEST_EQUAL(XdpFrameRxChecksumEvaluationNotChecked, Checksum->Layer3);
    TEST_EQUAL(XdpFrameRxChecksumEvaluationSucceeded, Checksum->Layer4);
    TEST_EQUAL(XdpFrameLayer4TypeUdp, Layout->Layer4Type);

    XskRingConsumerRelease(&Xsk.Rings.Rx, 1);
}

VOID
GenericTxChecksumOffloadConfig()
{
    const auto &If = FnMpIf;
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    const BOOLEAN Rx = FALSE, Tx = TRUE;

    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);
    EnableTxChecksumOffload(&Xsk);
    ActivateSocket(&Xsk, Rx, Tx);

    auto PlainXsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), Rx, Tx, XDP_GENERIC);

    XDP_CHECKSUM_CONFIGURATION ChecksumConfig;
    UINT32 OptionLength;

    TEST_FALSE(XskRingOffloadChanged(&Xsk.Rings.Tx));
    TEST_FALSE(XskRingOffloadChanged(&PlainXsk.Rings.Tx));

    OptionLength = 0;
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_MORE_DATA),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_TX_OFFLOAD_CURRENT_CONFIG_CHECKSUM, NULL, &OptionLength));
    TEST_TRUE(OptionLength >= XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1);

    OptionLength = 1;
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER),
        TryGetSockopt(
            Xsk.Handle.get(), XSK_SOCKOPT_TX_OFFLOAD_CURRENT_CONFIG_CHECKSUM, &ChecksumConfig,
            &OptionLength));
    TEST_TRUE(OptionLength == 0);

    OptionLength = sizeof(ChecksumConfig);
    GetSockopt(
        Xsk.Handle.get(), XSK_SOCKOPT_TX_OFFLOAD_CURRENT_CONFIG_CHECKSUM, &ChecksumConfig,
        &OptionLength);
    TEST_EQUAL(XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1, OptionLength);
    TEST_EQUAL(XDP_CHECKSUM_CONFIGURATION_REVISION_1, ChecksumConfig.Header.Revision);
    TEST_EQUAL(XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1, ChecksumConfig.Header.Size);
    TEST_FALSE(ChecksumConfig.Enabled);

    NDIS_OFFLOAD_PARAMETERS OffloadParams;
    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.IPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
    OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
    OffloadParams.UDPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
    OffloadParams.TCPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;
    OffloadParams.TCPIPv6Checksum = NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED;

    auto OffloadReset = MpUpdateTaskOffload(GenericMp, FnOffloadCurrentConfig, &OffloadParams);

    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS); // Give time for the offload change notification to occur.

    //
    // The offload bit should not be set unless the offload is enabled on the
    // socket.
    //
    TEST_TRUE(XskRingOffloadChanged(&Xsk.Rings.Tx));
    TEST_FALSE(XskRingOffloadChanged(&PlainXsk.Rings.Tx));

    //
    // Verify the current config is updated and the offload change flag has been
    // cleared.
    //
    OptionLength = sizeof(ChecksumConfig);
    GetSockopt(
        Xsk.Handle.get(), XSK_SOCKOPT_TX_OFFLOAD_CURRENT_CONFIG_CHECKSUM, &ChecksumConfig,
        &OptionLength);
    TEST_EQUAL(XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1, OptionLength);
    TEST_EQUAL(XDP_CHECKSUM_CONFIGURATION_REVISION_1, ChecksumConfig.Header.Revision);
    TEST_EQUAL(XDP_SIZEOF_CHECKSUM_CONFIGURATION_REVISION_1, ChecksumConfig.Header.Size);
    TEST_TRUE(ChecksumConfig.Enabled);
    TEST_TRUE(ChecksumConfig.TcpOptions);

    TEST_FALSE(XskRingOffloadChanged(&Xsk.Rings.Tx));
    TEST_FALSE(XskRingOffloadChanged(&PlainXsk.Rings.Tx));
}

VOID
GenericRxChecksumOffloadConfig() {
    //
    // Routine Description:
    //     This test verifies the RX checksum offload configuration.
    //
    // NOTE: TODO.
}

static
VOID
RxIndicate(
    _In_reads_bytes_(PayloadLength) UCHAR *Payload,
    _In_ SIZE_T PayloadLength,
    _In_ MY_SOCKET *Xsk,
    _In_ NET_IFINDEX IfIndex,
    _In_ UINT32 QueueId
    )
{
    auto GenericMp = MpOpenGeneric(IfIndex);

    DATA_BUFFER Buffer = {0};
    Buffer.DataOffset = 0;
    Buffer.DataLength = (UINT32)PayloadLength;
    Buffer.BufferLength = Buffer.DataLength;
    Buffer.VirtualAddress = Payload;

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, QueueId, &Buffer);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    SocketProduceRxFill(Xsk, 1);
    TEST_HRESULT(TryMpRxFlush(GenericMp));
}

static
VOID
TxIndicate(
    _In_reads_bytes_(PayloadLength) UCHAR *Payload,
    _In_ SIZE_T PayloadLength,
    _In_ MY_SOCKET *Xsk
    )
{
    UINT64 TxBuffer = SocketFreePop(Xsk);
    UCHAR *TxFrame = Xsk->Umem.Buffer.get() + TxBuffer;
    UINT32 TxFrameLength = (UINT32)PayloadLength;
    ASSERT(TxFrameLength <= Xsk->Umem.Reg.ChunkSize);
    RtlCopyMemory(TxFrame, Payload, PayloadLength);

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk->Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(Xsk, ProducerIndex++);
    TxDesc->Address.AddressAndOffset = TxBuffer;
    TxDesc->Length = TxFrameLength;
    XskRingProducerSubmit(&Xsk->Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS PokeResult;
    NotifySocket(Xsk->Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &PokeResult);
    TEST_EQUAL(0, PokeResult);
}

VOID
GenericXskWait(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    )
{
    auto If = FnMpIf;
    auto Xsk = SetupSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    const UINT32 WaitTimeoutMs = 1000;
    Stopwatch Timer;

    UCHAR Payload[] = "GenericXskWait";

    XSK_NOTIFY_FLAGS NotifyFlags = XSK_NOTIFY_FLAG_NONE;
    UINT32 ExpectedResult = 0;
    XSK_NOTIFY_RESULT_FLAGS NotifyResult;

    if (Rx) {
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_RX;
        ExpectedResult |= XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE;
    } else {
        //
        // Produce IO that does not satisfy the wait condition.
        //
        RxIndicate(Payload, sizeof(Payload), &Xsk, If.GetIfIndex(), FnMpIf.GetQueueId());
    }

    if (Tx) {
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
        ExpectedResult |= XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE;
    } else {
        //
        // Produce IO that does not satisfy the wait condition.
        //
        TxIndicate(Payload, sizeof(Payload), &Xsk);
    }

    //
    // Verify the wait times out when the requested IO is not available.
    //
    Timer.Reset();
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_TIMEOUT),
        TryNotifySocket(Xsk.Handle.get(), NotifyFlags, WaitTimeoutMs, &NotifyResult));
    Timer.ExpectElapsed(WaitTimeoutMs);

    //
    // On another thread, briefly delay execution to give the main test
    // thread a chance to begin waiting. Then, produce RX and TX.
    //
    struct DELAY_INDICATE_THREAD_CONTEXT {
        UCHAR *Payload;
        SIZE_T PayloadLength;
        MY_SOCKET *Xsk;
        NET_IFINDEX IfIndex;
        UINT32 QueueId;
        BOOLEAN Rx;
        BOOLEAN Tx;
    } Ctx;
    Ctx.Payload = Payload;
    Ctx.PayloadLength = sizeof(Payload);
    Ctx.Xsk = &Xsk;
    Ctx.IfIndex = If.GetIfIndex();
    Ctx.QueueId = FnMpIf.GetQueueId();
    Ctx.Rx = Rx;
    Ctx.Tx = Tx;

    CxPlatAsyncT<const DELAY_INDICATE_THREAD_CONTEXT> Async([](const DELAY_INDICATE_THREAD_CONTEXT *Ctx) {
        auto GenericMp = MpOpenGeneric(Ctx->IfIndex);

        CxPlatSleep(10);

        if (Ctx->Rx) {
            RxIndicate(Ctx->Payload, Ctx->PayloadLength, Ctx->Xsk, Ctx->IfIndex, Ctx->QueueId);
        }

        if (Ctx->Tx) {
            TxIndicate(Ctx->Payload, Ctx->PayloadLength, Ctx->Xsk);
        }
    }, &Ctx);

    //
    // Verify the wait succeeds if any of the conditions is true, and that all
    // conditions are eventually met.
    //
    do {
        Timer.Reset(TEST_TIMEOUT_ASYNC_MS);
        NotifySocket(Xsk.Handle.get(), NotifyFlags, WaitTimeoutMs, &NotifyResult);
        TEST_FALSE(Timer.IsExpired());
        TEST_NOT_EQUAL(0, (NotifyResult & ExpectedResult));

        if (NotifyResult & XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE) {
            XskRingConsumerRelease(&Xsk.Rings.Rx, 1);
        }

        if (NotifyResult & XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE) {
            XskRingConsumerRelease(&Xsk.Rings.Completion, 1);
        }

        ExpectedResult &= ~NotifyResult;
    } while (ExpectedResult != 0);

    //
    // Verify the wait does not succeed after consuming the requested IO.
    //
    Timer.Reset();
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_TIMEOUT),
        TryNotifySocket(Xsk.Handle.get(), NotifyFlags, WaitTimeoutMs, &NotifyResult));
    Timer.ExpectElapsed(WaitTimeoutMs);
}

VOID
GenericXskWaitAsync(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    )
{
    auto If = FnMpIf;
    auto Xsk = SetupSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    const UINT32 WaitTimeoutMs = 1000;
    OVERLAPPED ov = {0};
    wil::unique_handle iocp(CreateIoCompletionPort(Xsk.Handle.get(), NULL, 0, 0));

    UCHAR Payload[] = "GenericXskWaitAsync";

    XSK_NOTIFY_FLAGS NotifyFlags = XSK_NOTIFY_FLAG_NONE;
    UINT32 ExpectedResult = 0;
    XSK_NOTIFY_RESULT_FLAGS NotifyResult;

    if (Rx) {
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_RX;
        ExpectedResult |= XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE;
    } else {
        //
        // Produce IO that does not satisfy the wait condition.
        //
        RxIndicate(Payload, sizeof(Payload), &Xsk, If.GetIfIndex(), FnMpIf.GetQueueId());
    }

    if (Tx) {
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
        ExpectedResult |= XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE;
    } else {
        //
        // Produce IO that does not satisfy the wait condition.
        //
        TxIndicate(Payload, sizeof(Payload), &Xsk);
    }

    //
    // Verify the wait times out when the requested IO is not available.
    //
    DWORD bytes;
    ULONG_PTR key;
    OVERLAPPED *ovp;
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_IO_PENDING),
        TryNotifyAsync(Xsk.Handle.get(), NotifyFlags, &ov));
    TEST_FALSE(GetQueuedCompletionStatus(iocp.get(), &bytes, &key, &ovp, WaitTimeoutMs));
    TEST_EQUAL(WAIT_TIMEOUT, GetLastError());

    //
    // On another thread, briefly delay execution to give the main test
    // thread a chance to begin waiting. Then, produce RX and TX.
    //
    struct DELAY_INDICATE_THREAD_CONTEXT {
        UCHAR *Payload;
        SIZE_T PayloadLength;
        MY_SOCKET *Xsk;
        NET_IFINDEX IfIndex;
        UINT32 QueueId;
        BOOLEAN Rx;
        BOOLEAN Tx;
    } Ctx;
    Ctx.Payload = Payload;
    Ctx.PayloadLength = sizeof(Payload);
    Ctx.Xsk = &Xsk;
    Ctx.IfIndex = If.GetIfIndex();
    Ctx.QueueId = FnMpIf.GetQueueId();
    Ctx.Rx = Rx;
    Ctx.Tx = Tx;

    CxPlatAsyncT<const DELAY_INDICATE_THREAD_CONTEXT> Async([](const DELAY_INDICATE_THREAD_CONTEXT *Ctx) {
        auto GenericMp = MpOpenGeneric(Ctx->IfIndex);

        CxPlatSleep(10);

        if (Ctx->Rx) {
            RxIndicate(Ctx->Payload, Ctx->PayloadLength, Ctx->Xsk, Ctx->IfIndex, Ctx->QueueId);
        }

        if (Ctx->Tx) {
            TxIndicate(Ctx->Payload, Ctx->PayloadLength, Ctx->Xsk);
        }
    }, &Ctx);

    //
    // Verify the wait succeeds if any of the conditions is true, and that all
    // conditions are eventually met.
    //
    do {
        TEST_TRUE(GetQueuedCompletionStatus(iocp.get(), &bytes, &key, &ovp, WaitTimeoutMs));
        TEST_EQUAL(&ov, ovp);
        GetNotifyAsyncResult(&ov, &NotifyResult);
        TEST_NOT_EQUAL(0, (NotifyResult & ExpectedResult));

        if (NotifyResult & XSK_NOTIFY_RESULT_FLAG_RX_AVAILABLE) {
            XskRingConsumerRelease(&Xsk.Rings.Rx, 1);
        }

        if (NotifyResult & XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE) {
            XskRingConsumerRelease(&Xsk.Rings.Completion, 1);
        }

        ExpectedResult &= ~NotifyResult;

        if (ExpectedResult != 0) {
            HRESULT res = TryNotifyAsync(Xsk.Handle.get(), NotifyFlags, &ov);
            if (!SUCCEEDED(res)) {
                TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_IO_PENDING), res);
            }
        }
    } while (ExpectedResult != 0);

    //
    // Verify cancellation (happy path).
    //
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_IO_PENDING),
        TryNotifyAsync(Xsk.Handle.get(), NotifyFlags, &ov));
    TEST_FALSE(GetQueuedCompletionStatus(iocp.get(), &bytes, &key, &ovp, WaitTimeoutMs));
    TEST_EQUAL(WAIT_TIMEOUT, GetLastError());
    TEST_TRUE(CancelIoEx(Xsk.Handle.get(), &ov));
    TEST_FALSE(GetQueuedCompletionStatus(iocp.get(), &bytes, &key, &ovp, WaitTimeoutMs));
    TEST_EQUAL(ERROR_OPERATION_ABORTED, GetLastError());
}

VOID
GenericLwfDelayDetach(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    )
{
    const CHAR *DelayDetachTimeoutRegName = "GenericDelayDetachTimeoutSec";

    //
    // Configure the LWF delay detach timeout.
    //
    wil::unique_hkey XdpParametersKey;
    DWORD DelayTimeoutMs;
    DWORD DelayTimeoutSec;
    TEST_EQUAL(
        ERROR_SUCCESS,
        RegCreateKeyExA(
            HKEY_LOCAL_MACHINE,
            "System\\CurrentControlSet\\Services\\Xdp\\Parameters",
            0, NULL, REG_OPTION_VOLATILE, KEY_WRITE, NULL, &XdpParametersKey, NULL));

    //
    // Setup a generic socket to attach the LWF datapath, then close the socket
    // to trigger LWF datapath detach. Verify LWF datapath detach timestamp by
    // using XDPFNMP miniport pause timestamp as an approximation.
    //

    //
    // Verify LWF datapath detach delay does not impact LWF detach.
    //

    DelayTimeoutMs = 5 * (DWORD)MP_RESTART_TIMEOUT_MS;
    DelayTimeoutSec = DelayTimeoutMs / 1000;
    TEST_EQUAL(
        ERROR_SUCCESS,
        RegSetValueExA(
            XdpParametersKey.get(),
            DelayDetachTimeoutRegName,
            0, REG_DWORD, (BYTE *)&DelayTimeoutSec, sizeof(DelayTimeoutSec)));
    auto RegValueScopeGuard = wil::scope_exit([&]
    {
        //
        // Remove the registry key and restart the NDIS filter stack: the
        // delayed detach parameter does not reliably take effect until each
        // data path is fully detached. The only way to assure this (without
        // restarting XDP itself) is to restart the filter stack.
        //
        TEST_EQUAL(
            ERROR_SUCCESS,
            RegDeleteValueA(XdpParametersKey.get(), DelayDetachTimeoutRegName));
        CxPlatSleep(TEST_TIMEOUT_ASYNC_MS); // Give time for the reg change notification to occur.
        FnMpIf.Restart();
    });
    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS); // Give time for the reg change notification to occur.

    FnMpIf.Restart();
    {
        auto Xsk = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Rx, Tx, XDP_GENERIC);
    }
    Stopwatch Timer(MP_RESTART_TIMEOUT_MS);
    FnMpIf.Restart();
    TEST_FALSE(Timer.IsExpired());

    //
    // Verify LWF detach occurs at the expected time by observing FNMP pause events.
    //

    DelayTimeoutMs = TEST_TIMEOUT_ASYNC_MS * 3;
    DelayTimeoutSec = DelayTimeoutMs / 1000;
    TEST_EQUAL(
        ERROR_SUCCESS,
        RegSetValueExA(
            XdpParametersKey.get(),
            DelayDetachTimeoutRegName,
            0, REG_DWORD, (BYTE *)&DelayTimeoutSec, sizeof(DelayTimeoutSec)));
    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS); // Give time for the reg change notification to occur.

    FnMpIf.Restart();
    auto GenericMp = MpOpenGeneric(FnMpIf.GetIfIndex());
    LARGE_INTEGER LowerBoundTime;
    LARGE_INTEGER UpperBoundTime;
    LARGE_INTEGER LastMpPauseTime;
    LARGE_INTEGER SocketClosureTime;

    //
    // Simple case: close socket, wait for delay.
    //
    // 0s                                  -> close socket
    // DelayTimeoutMs - TEST_TIMEOUT_ASYNC_MS   -> lower bound
    // DelayTimeoutMs                      -> expected datapath detach
    // DelayTimeoutMs + TEST_TIMEOUT_ASYNC_MS   -> upper bound
    //
    {
        auto Xsk = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Rx, Tx, XDP_GENERIC);
        QueryPerformanceCounter(&SocketClosureTime);
    }
    CxPlatSleep(DelayTimeoutMs - TEST_TIMEOUT_ASYNC_MS);
    LastMpPauseTime = MpGetLastMiniportPauseTimestamp(GenericMp);
    TEST_TRUE(LastMpPauseTime.QuadPart < SocketClosureTime.QuadPart);
    QueryPerformanceCounter(&LowerBoundTime);
    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS * 2);
    QueryPerformanceCounter(&UpperBoundTime);

    LastMpPauseTime = MpGetLastMiniportPauseTimestamp(GenericMp);
    TEST_TRUE(LastMpPauseTime.QuadPart > LowerBoundTime.QuadPart);
    TEST_TRUE(LastMpPauseTime.QuadPart < UpperBoundTime.QuadPart);

    //
    // Reset case: close socket, open and close another socket, wait for delay.
    //
    // 0s                                   -> close socket
    // DelayTimeoutMs - TEST_TIMEOUT_ASYNC_MS    -> close another socket
    // DelayTimeoutMs + TEST_TIMEOUT_ASYNC_MS    -> lower bound
    // 2*DelayTimeoutMs - TEST_TIMEOUT_ASYNC_MS  -> expected datapath detach
    // 2*DelayTimeoutMs                     -> upper bound
    //
    {
        auto Xsk = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Rx, Tx, XDP_GENERIC);
        QueryPerformanceCounter(&SocketClosureTime);
    }
    CxPlatSleep(DelayTimeoutMs - TEST_TIMEOUT_ASYNC_MS);
    {
        auto Xsk = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Rx, Tx, XDP_GENERIC);
    }
    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS * 2);
    LastMpPauseTime = MpGetLastMiniportPauseTimestamp(GenericMp);
    TEST_TRUE(LastMpPauseTime.QuadPart < SocketClosureTime.QuadPart);
    QueryPerformanceCounter(&LowerBoundTime);
    CxPlatSleep(TEST_TIMEOUT_ASYNC_MS * 2);
    QueryPerformanceCounter(&UpperBoundTime);

    LastMpPauseTime = MpGetLastMiniportPauseTimestamp(GenericMp);
    TEST_TRUE(LastMpPauseTime.QuadPart > LowerBoundTime.QuadPart);
    TEST_TRUE(LastMpPauseTime.QuadPart < UpperBoundTime.QuadPart);
}

VOID
GenericLoopback(
    _In_ ADDRESS_FAMILY Af
    )
{
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    SOCKADDR_INET LocalSockAddr = {0};

    auto If = FnMpIf;
    auto Xsk = CreateAndActivateSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, TRUE, XDP_GENERIC);
    SocketProduceRxFill(&Xsk, 1);

    auto UdpSocket = CreateUdpSocket(Af, &If, &LocalPort);

    RemotePort = htons(4321);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    LocalSockAddr.si_family = Af;

    if (Af == AF_INET) {
        If.GetIpv4Address(&LocalIp.Ipv4);
        If.GetRemoteIpv4Address(&RemoteIp.Ipv4);
        LocalSockAddr.Ipv4.sin_addr = LocalIp.Ipv4;
    } else {
        If.GetIpv6Address(&LocalIp.Ipv6);
        If.GetRemoteIpv6Address(&RemoteIp.Ipv6);
        LocalSockAddr.Ipv6.sin6_addr = LocalIp.Ipv6;
    }

    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_ALL;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();

    wil::unique_handle ProgramHandle =
        CreateXdpProg(
            If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    unique_fnsock RawSocket;
    TEST_HRESULT(FnSockCreate(Af, SOCK_RAW, IPPROTO_IP, &RawSocket));
    TEST_NOT_NULL(RawSocket.get());

    TEST_HRESULT(FnSockBind(RawSocket.get(), (SOCKADDR *)&LocalSockAddr, sizeof(LocalSockAddr)));

    DWORD Opt = RCVALL_ON;
    ULONG BytesReturned;
    TEST_HRESULT(
        FnSockIoctl(
            RawSocket.get(), SIO_RCVALL, &Opt, sizeof(Opt), NULL, 0, &BytesReturned));

    //
    // When promiscuous mode is enabled from a RAW socket in TCPIP, locally sent
    // frames should get looped back at L2. These looped frames should be
    // ignored by XDP and be passed up the stack.
    //

    //
    // Send a frame.
    //

    UCHAR UdpPayload[] = "GenericLoopback";
    CHAR RecvPayload[sizeof(UdpPayload)] = {0};
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
    ASSERT(UdpFrameLength <= Xsk.Umem.Reg.ChunkSize);

    RtlCopyMemory(TxFrame, UdpFrame, UdpFrameLength);

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->Address.AddressAndOffset = TxBuffer;
    TxDesc->Length = UdpFrameLength;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    //
    // Verify that TCPIP received the frame.
    //

    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));

    //
    // Verify that the XSK did not receive the frame.
    //

    UINT32 ConsumerIndex;
    TEST_EQUAL(0, XskRingConsumerReserve(&Xsk.Rings.Rx, 1, &ConsumerIndex));
}

static
unique_malloc_ptr<XDP_RSS_CONFIGURATION>
GetXdpRss(
    _In_ const wil::unique_handle &InterfaceHandle,
    _Out_opt_ UINT32 *RssConfigSize = NULL
    )
{
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    UINT32 Size = 0;

    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_MORE_DATA),
        TryRssGet(InterfaceHandle.get(), NULL, &Size));
    TEST_TRUE(Size >= sizeof(*RssConfig.get()));

    RssConfig.reset((XDP_RSS_CONFIGURATION *)AllocMem(Size));
    TEST_NOT_NULL(RssConfig.get());

    RssGet(InterfaceHandle.get(), RssConfig.get(), &Size);
    TEST_EQUAL(RssConfig->Header.Revision, XDP_RSS_CONFIGURATION_REVISION_1);
    TEST_EQUAL(RssConfig->Header.Size, XDP_SIZEOF_RSS_CONFIGURATION_REVISION_1);

    if (RssConfigSize != NULL) {
        *RssConfigSize = Size;
    }

    return RssConfig;
}

static
VOID
GetXdpRssIndirectionTable(
    _In_ const TestInterface &If,
    _Out_ unique_malloc_ptr<PROCESSOR_NUMBER> &IndirectionTableOut,
    _Out_ UINT32 &IndirectionTableSizeOut
    )
{
    wil::unique_handle InterfaceHandle = InterfaceOpen(If.GetIfIndex());
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig = GetXdpRss(InterfaceHandle);

    IndirectionTableOut.reset((PROCESSOR_NUMBER *)AllocMem(RssConfig->IndirectionTableSize));
    TEST_NOT_NULL(IndirectionTableOut.get());
    PROCESSOR_NUMBER *IndirectionTable =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfig.get(), RssConfig->IndirectionTableOffset);
    RtlCopyMemory(IndirectionTableOut.get(), IndirectionTable, RssConfig->IndirectionTableSize);
    IndirectionTableSizeOut = RssConfig->IndirectionTableSize;
}

static
VOID
SetXdpRss(
    _In_ const TestInterface &If,
    _In_ const wil::unique_handle &InterfaceHandle,
    _In_ const unique_malloc_ptr<PROCESSOR_NUMBER> &IndirectionTable,
    _In_ UINT32 IndirectionTableSize
    )
{
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    auto AdapterMp = MpOpenAdapter(If.GetIfIndex());
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    UINT16 HashSecretKeySize = 40;
    UINT32 RssConfigSize = sizeof(*RssConfig) + HashSecretKeySize + IndirectionTableSize;

    //
    // Form the XdpSetRss input.
    //

    RssConfig.reset((XDP_RSS_CONFIGURATION *)AllocMem(RssConfigSize));
    TEST_NOT_NULL(RssConfig.get());

    XdpInitializeRssConfiguration(RssConfig.get(), RssConfigSize);
    RssConfig->HashSecretKeyOffset = sizeof(*RssConfig);
    RssConfig->IndirectionTableOffset = RssConfig->HashSecretKeyOffset + HashSecretKeySize;

    PROCESSOR_NUMBER *IndirectionTableDst =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfig.get(), RssConfig->IndirectionTableOffset);
    RtlCopyMemory(IndirectionTableDst, IndirectionTable.get(), IndirectionTableSize);

    RssConfig->Flags =
        XDP_RSS_FLAG_SET_HASH_TYPE | XDP_RSS_FLAG_SET_HASH_SECRET_KEY |
        XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    RssConfig->HashType = XDP_RSS_HASH_TYPE_TCP_IPV4 | XDP_RSS_HASH_TYPE_TCP_IPV6;
    RssConfig->HashSecretKeySize = HashSecretKeySize;
    RssConfig->IndirectionTableSize = (USHORT)IndirectionTableSize;

    //
    // Set an OID filter, initiate XdpSetRss, verify the resulting OID, close
    // the handle to allow OID completion.
    //

    OID_KEY Key;
    InitializeOidKey(&Key, OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRequestSetInformation);
    MpOidFilter(AdapterMp, &Key, 1);

    struct TRY_RSS_SET_THREAD_CONTEXT {
        HANDLE InterfaceHandle;
        XDP_RSS_CONFIGURATION *RssConfig;
        UINT32 RssConfigSize;
        HRESULT Result;
    } Ctx;
    Ctx.InterfaceHandle = InterfaceHandle.get();
    Ctx.RssConfig = RssConfig.get();
    Ctx.RssConfigSize = RssConfigSize;

    CxPlatAsyncT<TRY_RSS_SET_THREAD_CONTEXT> Async([](TRY_RSS_SET_THREAD_CONTEXT *Ctx) {
        Ctx->Result = TryRssSet(Ctx->InterfaceHandle, Ctx->RssConfig, Ctx->RssConfigSize);
    }, &Ctx);

    UINT32 OidInfoBufferLength;
    unique_malloc_ptr<VOID> OidInfoBuffer =
        MpOidAllocateAndGetRequest(AdapterMp, Key, &OidInfoBufferLength);

    NDIS_RECEIVE_SCALE_PARAMETERS *NdisParams =
        (NDIS_RECEIVE_SCALE_PARAMETERS *)OidInfoBuffer.get();
    TEST_EQUAL(NdisParams->IndirectionTableSize, IndirectionTableSize);
    PROCESSOR_NUMBER *NdisIndirectionTable =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(NdisParams, NdisParams->IndirectionTableOffset);
    TEST_TRUE(RtlEqualMemory(NdisIndirectionTable, IndirectionTable.get(), IndirectionTableSize));

    AdapterMp.reset();
    TEST_TRUE(Async.WaitFor(TEST_TIMEOUT_ASYNC_MS));
    TEST_HRESULT(Ctx.Result);
}

static
VOID
IndicateOnAllActiveRssQueues(
    _In_ TestInterface &If,
    _In_ UINT32 NumRssQueues
    )
{
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    UCHAR Payload[] = "IndicateOnAllActiveRssQueuesQ#";

    DATA_BUFFER Buffer = {0};
    Buffer.DataOffset = 0;
    Buffer.DataLength = sizeof(Payload);
    Buffer.BufferLength = Buffer.DataLength;
    Buffer.VirtualAddress = Payload;

    for (UINT32 i = 0; i < NumRssQueues; i++) {
        Payload[sizeof(Payload) - 1] = (UCHAR)i;
        RX_FRAME Frame;
        RxInitializeFrame(&Frame, i, &Buffer);
        TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));

        DATA_FLUSH_OPTIONS FlushOptions = {0};
        FlushOptions.Flags.RssCpu = TRUE;
        FlushOptions.RssCpuQueueId = i;
        TEST_HRESULT(TryMpRxFlush(GenericMp, &FlushOptions));
    }
}

static
VOID
PrintProcArray(
    _In_ const char* Prefix,
    _In_ const CxPlatVector<UINT32> &ProcArray
    )
{
    CHAR Msg[1024] = {0};

    strcat_s(Msg, sizeof(Msg), Prefix);
    strcat_s(Msg, sizeof(Msg), "[");
    for (int i = 0; i < ProcArray.size(); i++) {
        CHAR NumStr[16];
        _ultoa_s(ProcArray[i], NumStr, sizeof(NumStr), 10);
        strcat_s(Msg, sizeof(Msg), NumStr);
        if (i != ProcArray.size() - 1) {
            strcat_s(Msg, sizeof(Msg), ",");
        }
    }
    strcat_s(Msg, sizeof(Msg), "]");

    TraceVerbose("%s", Msg);
}

static
VOID
VerifyRssSettings(
    _In_ const TestInterface &If,
    _In_ const unique_malloc_ptr<PROCESSOR_NUMBER> &ExpectedXdpIndirectionTable,
    _In_ UINT32 ExpectedXdpIndirectionTableSize
    )
{
    unique_malloc_ptr<PROCESSOR_NUMBER> IndirectionTable;
    UINT32 IndirectionTableSize;

    GetXdpRssIndirectionTable(If, IndirectionTable, IndirectionTableSize);
    TEST_EQUAL(IndirectionTableSize, ExpectedXdpIndirectionTableSize);
    TEST_TRUE(
        RtlEqualMemory(
            IndirectionTable.get(), ExpectedXdpIndirectionTable.get(), IndirectionTableSize));
}

static
_Success_(return)
BOOLEAN
FindProcessorNumber(
    _In_ const CxPlatVector<PROCESSOR_NUMBER> &Array,
    _In_ const PROCESSOR_NUMBER &Processor,
    _Out_ ULONG *Index
    )
{
    for (int i = 0; i < Array.size(); i++) {
        if (Processor.Group == Array[i].Group && Processor.Number == Array[i].Number) {
            *Index = i;
            return TRUE;
        }
    }

    return FALSE;
}

static
CxPlatVector<PROCESSOR_NUMBER>
GetRssProcessorSetFromIndirectionTable(
    _In_ const PROCESSOR_NUMBER *IndirectionTable,
    _In_ UINT32 IndirectionTableSize
    )
{
    CxPlatVector<PROCESSOR_NUMBER> RssProcessors;

    //
    // Convert indirection table to processor set.
    //
    for (UINT32 Index = 0;
        Index < IndirectionTableSize / sizeof(*IndirectionTable);
        Index++) {
        PROCESSOR_NUMBER Processor = IndirectionTable[Index];
        ULONG FoundIndex;
        if (!FindProcessorNumber(RssProcessors, Processor, &FoundIndex)) {
            TEST_TRUE(RssProcessors.push_back(Processor));
        }
    }

    return RssProcessors;
}

static
VOID
VerifyRssDatapath(
    _In_ TestInterface &If,
    _In_ const unique_malloc_ptr<PROCESSOR_NUMBER> &IndirectionTable,
    _In_ UINT32 IndirectionTableSize
    )
{
    CxPlatVector<PROCESSOR_NUMBER> RssProcessors =
        GetRssProcessorSetFromIndirectionTable(IndirectionTable.get(), IndirectionTableSize);

    //
    // Indicate RX on each miniport RSS queue and capture all packets after XDP
    // passes them up.
    //

    auto DefaultLwf = LwfOpenDefault(If.GetIfIndex());
    UCHAR Pattern = 0x00;
    UCHAR Mask = 0x00;
    auto LwfFilter = LwfRxFilter(DefaultLwf, &Pattern, &Mask, sizeof(Pattern));

    IndicateOnAllActiveRssQueues(If, (UINT32)RssProcessors.size());

    //
    // Verify that the resulting indications are as expected.
    //
    // XDP's current level of RSS offload data path translation is zeroing out
    // the NBL hash OOB and maintaining the same processor for indication. So
    // verify that a single packet was indicated on each processor in the
    // miniport's RSS processor set and that its RSS hash is 0.
    //

    UINT32 NumRssProcessors = (UINT32)RssProcessors.size();
    for (UINT32 Index = 0; Index < NumRssProcessors; Index++) {
        auto Frame = LwfRxAllocateAndGetFrame(DefaultLwf, Index);
        PROCESSOR_NUMBER Processor = Frame->Output.ProcessorNumber;
        ULONG FoundIndex;
        TEST_TRUE(FindProcessorNumber(RssProcessors, Processor, &FoundIndex));
        RssProcessors.eraseAt(FoundIndex);
        TEST_EQUAL(0, Frame->Output.RssHash);
    }

    UINT32 FrameLength = 0;
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        LwfRxGetFrame(DefaultLwf, NumRssProcessors, &FrameLength, NULL));
}

VOID
OffloadRssError()
{
    wil::unique_handle InterfaceHandle;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    UINT16 IndirectionTableSize = 1 * sizeof(PROCESSOR_NUMBER);
    UINT32 RssConfigSize = sizeof(*RssConfig) + IndirectionTableSize;

    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    //
    // Open with invalid IfIndex.
    //
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        TryInterfaceOpen(MAXUINT32, InterfaceHandle));

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());

    //
    // Work around issue #3: if TCPIP hasn't already plumbed RSS configuration,
    // XDP fails to partially set RSS. Wait for TCPIP's configuration before
    // continuing with this test case.
    //
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    HRESULT CurrentRssResult;
    do {
        UINT32 CurrentRssConfigSize = 0;
        CurrentRssResult = TryRssGet(InterfaceHandle.get(), NULL, &CurrentRssConfigSize);
        if (CurrentRssResult == HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), CurrentRssResult);

    RssConfig.reset((XDP_RSS_CONFIGURATION *)AllocMem(RssConfigSize));
    TEST_NOT_NULL(RssConfig.get());

    XdpInitializeRssConfiguration(RssConfig.get(), RssConfigSize);
    RssConfig->Flags = XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    RssConfig->IndirectionTableSize = IndirectionTableSize;
    RssConfig->IndirectionTableOffset = sizeof(*RssConfig);

    PROCESSOR_NUMBER *IndirectionTable =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfig.get(), RssConfig->IndirectionTableOffset);
    IndirectionTable[0].Number = 1;
    RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);
    InterfaceHandle.reset();
    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());

    for (auto Case : RxTxTestCases) {
        auto Socket = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);
        RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);
    }

    //
    // Set while another handle has already set.
    //

    RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);

    wil::unique_handle InterfaceHandle2 = InterfaceOpen(FnMpIf.GetIfIndex());
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_BAD_COMMAND),
        TryRssSet(InterfaceHandle2.get(), RssConfig.get(), RssConfigSize));
}

VOID
OffloadRssReference()
{
    wil::unique_handle InterfaceHandle;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> ModifiedRssConfig;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> OriginalRssConfig;
    UINT32 RssConfigSize;
    UINT32 ModifiedRssConfigSize;
    UINT32 OriginalRssConfigSize;

    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    for (auto Case : RxTxTestCases) {
        //
        // Get original RSS settings.
        //
        InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
        OriginalRssConfig = GetXdpRss(InterfaceHandle, &OriginalRssConfigSize);

        //
        // Configure new RSS settings.
        //

        ModifiedRssConfigSize = OriginalRssConfigSize;
        ModifiedRssConfig.reset((XDP_RSS_CONFIGURATION *)AllocMem(ModifiedRssConfigSize));
        TEST_NOT_NULL(ModifiedRssConfig.get());

        XdpInitializeRssConfiguration(ModifiedRssConfig.get(), ModifiedRssConfigSize);
        ModifiedRssConfig->Flags = XDP_RSS_FLAG_SET_HASH_TYPE;
        ModifiedRssConfig->HashType =
            OriginalRssConfig->HashType ^ (XDP_RSS_HASH_TYPE_TCP_IPV4 | XDP_RSS_HASH_TYPE_TCP_IPV6);
        TEST_TRUE(ModifiedRssConfig->HashType != OriginalRssConfig->HashType);

        RssSet(InterfaceHandle.get(), ModifiedRssConfig.get(), ModifiedRssConfigSize);

        //
        // Bind socket (and setup RX program).
        //
        auto Socket =
            SetupSocket(
                FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);

        //
        // Close RSS handle.
        //
        InterfaceHandle.reset();

        //
        // Verify RSS settings restored.
        //
        InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
        RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);
        TEST_EQUAL(RssConfig->HashType, OriginalRssConfig->HashType);
    }
}

VOID
OffloadRssUnchanged()
{
    wil::unique_handle InterfaceHandle;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    UINT32 RssConfigSize;
    UCHAR *HashSecretKey;
    PROCESSOR_NUMBER *IndirectionTable;

    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());

    //
    // Hash type.
    //
    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);
    UINT32 ExpectedHashType = RssConfig->HashType;
    RssConfig->Flags = XDP_RSS_FLAG_SET_HASH_SECRET_KEY | XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    RssConfig->HashType = 0;
    RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);
    RssConfig = GetXdpRss(InterfaceHandle);
    TEST_EQUAL(RssConfig->HashType, ExpectedHashType);

    //
    // Hash secret key.
    //
    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);
    HashSecretKey = (UCHAR *)RTL_PTR_ADD(RssConfig.get(), RssConfig->HashSecretKeyOffset);
    UCHAR ExpectedHashSecretKey[40];
    UINT32 ExpectedHashSecretKeySize = RssConfig->HashSecretKeySize;
    ASSERT(ExpectedHashSecretKeySize <= sizeof(ExpectedHashSecretKey));
    RtlCopyMemory(&ExpectedHashSecretKey, HashSecretKey, RssConfig->HashSecretKeySize);
    RssConfig->Flags = XDP_RSS_FLAG_SET_HASH_TYPE | XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    RtlZeroMemory(HashSecretKey, RssConfig->HashSecretKeySize);
    RssConfig->HashSecretKeySize = 0;
    RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);
    RssConfig = GetXdpRss(InterfaceHandle);
    TEST_EQUAL(RssConfig->HashSecretKeySize, ExpectedHashSecretKeySize);
    HashSecretKey = (UCHAR *)RTL_PTR_ADD(RssConfig.get(), RssConfig->HashSecretKeyOffset);
    TEST_TRUE(RtlEqualMemory(HashSecretKey, &ExpectedHashSecretKey, ExpectedHashSecretKeySize));

    //
    // Indirection table.
    //
    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);
    IndirectionTable =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfig.get(), RssConfig->IndirectionTableOffset);
    PROCESSOR_NUMBER ExpectedIndirectionTable[128];
    UINT32 ExpectedIndirectionTableSize = RssConfig->IndirectionTableSize;
    ASSERT(ExpectedIndirectionTableSize <= sizeof(ExpectedIndirectionTable));
    RtlCopyMemory(&ExpectedIndirectionTable, IndirectionTable, RssConfig->IndirectionTableSize);
    RssConfig->Flags = XDP_RSS_FLAG_SET_HASH_TYPE | XDP_RSS_FLAG_SET_HASH_SECRET_KEY;
    RtlZeroMemory(IndirectionTable, RssConfig->IndirectionTableSize);
    RssConfig->IndirectionTableSize = 0;
    RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);
    RssConfig = GetXdpRss(InterfaceHandle);
    TEST_EQUAL(RssConfig->IndirectionTableSize, ExpectedIndirectionTableSize);
    IndirectionTable =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfig.get(), RssConfig->IndirectionTableOffset);
    TEST_TRUE(
        RtlEqualMemory(IndirectionTable, &ExpectedIndirectionTable, ExpectedIndirectionTableSize));
}

VOID
OffloadRssInterfaceRestart()
{
    wil::unique_handle InterfaceHandle;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> OriginalRssConfig;
    UINT32 RssConfigSize;
    UINT32 OriginalRssConfigSize;
    VOID *HashSecretKey;
    VOID *OriginalHashSecretKey;
    VOID *IndirectionTable;
    VOID *OriginalIndirectionTable;

    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    //
    // Get original RSS settings and configure new settings.
    //

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
    OriginalRssConfig = GetXdpRss(InterfaceHandle, &OriginalRssConfigSize);

    RssConfig.reset((XDP_RSS_CONFIGURATION *)AllocMem(OriginalRssConfigSize));
    TEST_NOT_NULL(RssConfig.get());
    RtlCopyMemory(RssConfig.get(), OriginalRssConfig.get(), OriginalRssConfigSize);
    RssConfigSize = OriginalRssConfigSize;

    RssConfig->Flags = XDP_RSS_FLAG_SET_HASH_TYPE;
    RssConfig->HashType =
        RssConfig->HashType ^ (XDP_RSS_HASH_TYPE_TCP_IPV4 | XDP_RSS_HASH_TYPE_TCP_IPV6);

    RssConfig->Flags = XDP_RSS_FLAG_SET_HASH_SECRET_KEY;
    TEST_TRUE(RssConfig->HashSecretKeySize > 0);
    HashSecretKey = (UCHAR *)RTL_PTR_ADD(RssConfig.get(), RssConfig->HashSecretKeyOffset);
    ((UCHAR *)HashSecretKey)[0] = ~(((UCHAR *)HashSecretKey)[0]);

    RssConfig->Flags = XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    TEST_TRUE(RssConfig->IndirectionTableSize >= (2 * sizeof(PROCESSOR_NUMBER)));
    RssConfig->IndirectionTableSize /= 2;

    RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);

    FnMpIf.Restart();

    //
    // Verify old handle is invalid.
    //

    UINT32 Size = RssConfigSize;
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_READY),
        TryRssGet(InterfaceHandle.get(), RssConfig.get(), &Size));

    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_READY),
        TryRssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize));

    InterfaceHandle.reset();

    //
    // Verify original RSS settings are restored.
    //

    //
    // Wait for the interface to be up and RSS to be configured by the upper
    // layer protocol.
    //
    Stopwatch Watchdog(MP_RESTART_TIMEOUT_MS);
    HRESULT Result = S_OK;
    do {
        Result = TryInterfaceOpen(FnMpIf.GetIfIndex(), InterfaceHandle);
        if (FAILED(Result)) {
            TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), Result);
            continue;
        }

        Size = 0;
        Result = TryRssGet(InterfaceHandle.get(), NULL, &Size);
        if (Result == HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);

    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);

    TEST_EQUAL(RssConfig->Flags, OriginalRssConfig->Flags);
    TEST_EQUAL(RssConfig->HashType, OriginalRssConfig->HashType);

    TEST_EQUAL(RssConfig->HashSecretKeySize, OriginalRssConfig->HashSecretKeySize);
    HashSecretKey = RTL_PTR_ADD(RssConfig.get(), RssConfig->HashSecretKeyOffset);
    OriginalHashSecretKey =
        RTL_PTR_ADD(OriginalRssConfig.get(), OriginalRssConfig->HashSecretKeyOffset);
    TEST_TRUE(
        RtlEqualMemory(HashSecretKey, OriginalHashSecretKey, RssConfig->HashSecretKeySize));

    TEST_EQUAL(RssConfig->IndirectionTableSize, OriginalRssConfig->IndirectionTableSize);
    IndirectionTable = RTL_PTR_ADD(RssConfig.get(), RssConfig->IndirectionTableOffset);
    OriginalIndirectionTable =
        RTL_PTR_ADD(OriginalRssConfig.get(), OriginalRssConfig->IndirectionTableOffset);
    TEST_TRUE(
        RtlEqualMemory(IndirectionTable, OriginalIndirectionTable, RssConfig->IndirectionTableSize));
}

VOID
OffloadRssUpperSet()
{
    wil::unique_handle InterfaceHandle;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    unique_malloc_ptr<NDIS_RECEIVE_SCALE_PARAMETERS> NdisRssParams;
    unique_malloc_ptr<NDIS_RECEIVE_SCALE_PARAMETERS> OriginalNdisRssParams;
    NDIS_RECEIVE_SCALE_PARAMETERS UpperNdisRssParams = {0};
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> LowerRssConfig;
    UINT32 RssConfigSize;
    UINT32 NdisRssParamsSize;
    UINT32 OriginalNdisRssParamsSize;
    UINT32 UpperNdisRssParamsSize;
    UINT32 LowerRssConfigSize;
    const UINT32 LowerXdpRssHashType = XDP_RSS_HASH_TYPE_TCP_IPV4;
    const UINT32 UpperXdpRssHashType = XDP_RSS_HASH_TYPE_TCP_IPV6;
    const UINT32 UpperNdisRssHashType = NDIS_HASH_TCP_IPV6;
    OID_KEY OidKey;
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    //
    // Get original settings (both XDP and NDIS formats for convenience).
    //

    InitializeOidKey(&OidKey, OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRequestQueryInformation);
    OriginalNdisRssParams =
        LwfOidAllocateAndSubmitRequest<NDIS_RECEIVE_SCALE_PARAMETERS>(
            DefaultLwf, OidKey, &OriginalNdisRssParamsSize);

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);

    //
    // Set lower edge settings via XDP.
    //
    LowerRssConfig.reset((XDP_RSS_CONFIGURATION *)AllocMem(RssConfigSize));
    TEST_NOT_NULL(LowerRssConfig.get());
    LowerRssConfigSize = RssConfigSize;
    RtlCopyMemory(LowerRssConfig.get(), RssConfig.get(), RssConfigSize);
    LowerRssConfig->Flags = XDP_RSS_FLAG_SET_HASH_TYPE;
    LowerRssConfig->HashType = LowerXdpRssHashType;
    RssSet(InterfaceHandle.get(), LowerRssConfig.get(), LowerRssConfigSize);

    //
    // Set upper edge settings via NDIS.
    //
    InitializeOidKey(&OidKey, OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRequestSetInformation);
    UpperNdisRssParams.Header.Type = NDIS_OBJECT_TYPE_RSS_PARAMETERS;
    UpperNdisRssParams.Header.Revision = NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2;
    UpperNdisRssParams.Header.Size = NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2;
    UpperNdisRssParams.Flags =
        NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED |
        NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED |
        NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED;
    UpperNdisRssParams.HashInformation =
        NDIS_RSS_HASH_INFO_FROM_TYPE_AND_FUNC(
            UpperNdisRssHashType, NdisHashFunctionToeplitz);
    UpperNdisRssParamsSize = sizeof(UpperNdisRssParams);
    TEST_HRESULT(
        LwfOidSubmitRequest(
            DefaultLwf, OidKey, &UpperNdisRssParamsSize, &UpperNdisRssParams));

    //
    // Upon test case exit, revert to original upper edge settings.
    //
    auto RssScopeGuard = wil::scope_exit([&]
    {
        if (OriginalNdisRssParams.get() != NULL) {
            OidKey.Oid = OID_GEN_RECEIVE_SCALE_PARAMETERS;
            OidKey.RequestType = NdisRequestSetInformation;
            LwfOidSubmitRequest(
                DefaultLwf, OidKey, &OriginalNdisRssParamsSize, OriginalNdisRssParams.get());
        }
    });

    //
    // Verify upper edge settings.
    //
    InitializeOidKey(&OidKey, OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRequestQueryInformation);
    NdisRssParams =
        LwfOidAllocateAndSubmitRequest<NDIS_RECEIVE_SCALE_PARAMETERS>(
            DefaultLwf, OidKey, &NdisRssParamsSize);
    TEST_EQUAL(NdisRssParams->HashInformation, UpperNdisRssParams.HashInformation);
    TEST_TRUE(UpperNdisRssParams.HashInformation != OriginalNdisRssParams->HashInformation);

    //
    // Verify lower edge settings persist.
    //
    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);
    TEST_EQUAL(RssConfig->HashType, LowerRssConfig->HashType);

    //
    // Reset lower edge settings.
    //
    InterfaceHandle.reset();

    //
    // Verify lower edge settings now match upper edge.
    //
    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);
    TEST_EQUAL(RssConfig->HashType, UpperXdpRssHashType);
}

static
VOID
CreateIndirectionTable(
    _In_ const CxPlatVector<UINT32> &ProcessorIndices,
    _Out_ unique_malloc_ptr<PROCESSOR_NUMBER> &IndirectionTable,
    _Out_ UINT32 *IndirectionTableSize
    )
{
    *IndirectionTableSize = (UINT32)ProcessorIndices.size() * sizeof(*IndirectionTable);

    IndirectionTable.reset((PROCESSOR_NUMBER *)AllocMem(*IndirectionTableSize));
    TEST_NOT_NULL(IndirectionTable.get());

    RtlZeroMemory(IndirectionTable.get(), *IndirectionTableSize);
    for (UINT32 i = 0; i < ProcessorIndices.size(); i++) {
        ProcessorIndexToProcessorNumber(ProcessorIndices[i], &IndirectionTable.get()[i]);
    }
}

VOID
OffloadRssSingleSet(
    _In_ const CxPlatVector<UINT32> &ProcessorIndices
    )
{
    unique_malloc_ptr<PROCESSOR_NUMBER> IndirectionTable;
    unique_malloc_ptr<PROCESSOR_NUMBER> OldIndirectionTable;
    unique_malloc_ptr<PROCESSOR_NUMBER> ResetIndirectionTable;
    wil::unique_handle InterfaceHandle;
    UINT32 IndirectionTableSize;
    UINT32 OldIndirectionTableSize;
    UINT32 ResetIndirectionTableSize;

    PrintProcArray("OffloadRssSingleSet:", ProcessorIndices);

    CreateIndirectionTable(ProcessorIndices, IndirectionTable, &IndirectionTableSize);

    GetXdpRssIndirectionTable(FnMpIf, OldIndirectionTable, OldIndirectionTableSize);

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
    SetXdpRss(FnMpIf, InterfaceHandle, IndirectionTable, IndirectionTableSize);
    VerifyRssSettings(FnMpIf, IndirectionTable, IndirectionTableSize);
    VerifyRssDatapath(FnMpIf, IndirectionTable, IndirectionTableSize);

    InterfaceHandle.reset();
    GetXdpRssIndirectionTable(FnMpIf, ResetIndirectionTable, ResetIndirectionTableSize);
    TEST_EQUAL(ResetIndirectionTableSize, OldIndirectionTableSize);
    TEST_TRUE(
        RtlEqualMemory(
            ResetIndirectionTable.get(), OldIndirectionTable.get(), ResetIndirectionTableSize));
}

VOID
OffloadRssSubsequentSet(
    _In_ const CxPlatVector<UINT32> &ProcessorIndices1,
    _In_ const CxPlatVector<UINT32> &ProcessorIndices2
    )
{
    wil::unique_handle InterfaceHandle;
    unique_malloc_ptr<PROCESSOR_NUMBER> IndirectionTable1;
    unique_malloc_ptr<PROCESSOR_NUMBER> IndirectionTable2;
    UINT32 IndirectionTable1Size;
    UINT32 IndirectionTable2Size;

    TraceVerbose("OffloadRssSubsequentSet");
    PrintProcArray("XDP1:", ProcessorIndices1);
    PrintProcArray("XDP2:", ProcessorIndices2);

    CreateIndirectionTable(ProcessorIndices1, IndirectionTable1, &IndirectionTable1Size);
    CreateIndirectionTable(ProcessorIndices2, IndirectionTable2, &IndirectionTable2Size);

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());

    SetXdpRss(FnMpIf, InterfaceHandle, IndirectionTable1, IndirectionTable2Size);
    VerifyRssSettings(FnMpIf, IndirectionTable1, IndirectionTable2Size);
    VerifyRssDatapath(FnMpIf, IndirectionTable1, IndirectionTable2Size);

    SetXdpRss(FnMpIf, InterfaceHandle, IndirectionTable2, IndirectionTable2Size);
    VerifyRssSettings(FnMpIf, IndirectionTable2, IndirectionTable2Size);
    VerifyRssDatapath(FnMpIf, IndirectionTable2, IndirectionTable2Size);
}

VOID
OffloadRssSet()
{
    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    CxPlatVector<UINT32> RssProcs1;
    CxPlatVector<UINT32> RssProcs2;

    RssProcs1.clear();
    TEST_TRUE(RssProcs1.push_back(0));
    OffloadRssSingleSet(RssProcs1);

    RssProcs1.clear();
    TEST_TRUE(RssProcs1.push_back(1));
    OffloadRssSingleSet(RssProcs1);

    RssProcs1.clear();
    TEST_TRUE(RssProcs1.push_back(0));
    TEST_TRUE(RssProcs1.push_back(1));
    OffloadRssSingleSet(RssProcs1);

    RssProcs1.clear();
    TEST_TRUE(RssProcs1.push_back(0));
    RssProcs2.clear();
    TEST_TRUE(RssProcs2.push_back(1));
    OffloadRssSubsequentSet(RssProcs1, RssProcs2);

    if (GetProcessorCount() >= 4) {
        RssProcs1.clear();
        TEST_TRUE(RssProcs1.push_back(0));
        TEST_TRUE(RssProcs1.push_back(2));
        OffloadRssSingleSet(RssProcs1);

        RssProcs1.clear();
        TEST_TRUE(RssProcs1.push_back(1));
        TEST_TRUE(RssProcs1.push_back(3));
        OffloadRssSingleSet(RssProcs1);

        RssProcs1.clear();
        TEST_TRUE(RssProcs1.push_back(0));
        TEST_TRUE(RssProcs1.push_back(1));
        TEST_TRUE(RssProcs1.push_back(2));
        TEST_TRUE(RssProcs1.push_back(3));
        OffloadRssSingleSet(RssProcs1);

        RssProcs1.clear();
        TEST_TRUE(RssProcs1.push_back(0));
        TEST_TRUE(RssProcs1.push_back(2));
        RssProcs2.clear();
        TEST_TRUE(RssProcs2.push_back(1));
        TEST_TRUE(RssProcs2.push_back(3));
        OffloadRssSubsequentSet(RssProcs1, RssProcs2);
    }
}

VOID
OffloadRssCapabilities()
{
    wil::unique_handle InterfaceHandle;
    unique_malloc_ptr<XDP_RSS_CAPABILITIES> RssCapabilities;
    UINT32 Size = 0;

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());

    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_MORE_DATA),
        TryRssGetCapabilities(InterfaceHandle.get(), NULL, &Size));
    TEST_EQUAL(Size, XDP_SIZEOF_RSS_CAPABILITIES_REVISION_2);

    RssCapabilities.reset((XDP_RSS_CAPABILITIES *)AllocMem(Size));
    TEST_NOT_NULL(RssCapabilities.get());

    RssGetCapabilities(InterfaceHandle.get(), RssCapabilities.get(), &Size);
    TEST_EQUAL(RssCapabilities->Header.Revision, XDP_RSS_CAPABILITIES_REVISION_2);
    TEST_EQUAL(RssCapabilities->Header.Size, XDP_SIZEOF_RSS_CAPABILITIES_REVISION_2);
    TEST_EQUAL(
        RssCapabilities->HashTypes,
        (XDP_RSS_HASH_TYPE_IPV4 | XDP_RSS_HASH_TYPE_IPV6 |
            XDP_RSS_HASH_TYPE_TCP_IPV4 | XDP_RSS_HASH_TYPE_TCP_IPV6));
    TEST_EQUAL(RssCapabilities->HashSecretKeySize, 40);
    TEST_EQUAL(RssCapabilities->NumberOfReceiveQueues, FNMP_DEFAULT_RSS_QUEUES);
    TEST_EQUAL(RssCapabilities->NumberOfIndirectionTableEntries, FNMP_MAX_RSS_INDIR_COUNT);
}

VOID
OffloadRssReset()
{
    auto &If = FnMpIf;
    unique_malloc_ptr<PROCESSOR_NUMBER> IndirectionTable;
    unique_malloc_ptr<PROCESSOR_NUMBER> OriginalIndirectionTable;
    unique_malloc_ptr<PROCESSOR_NUMBER> ResetIndirectionTable;
    UINT32 IndirectionTableSize;
    UINT32 OriginalIndirectionTableSize;

    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    auto InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
    auto AdapterMp = MpOpenAdapter(If.GetIfIndex());

    //
    // Query the original RSS table. This is what XDP should revert to when the
    // interface is being torn down.
    //
    GetXdpRssIndirectionTable(FnMpIf, OriginalIndirectionTable, OriginalIndirectionTableSize);

    //
    // Create and set a new RSS table.
    //

    CxPlatVector<UINT32> ProcessorIndices;
    TEST_TRUE(ProcessorIndices.push_back(1));
    TEST_TRUE(ProcessorIndices.push_back(0));
    CreateIndirectionTable(ProcessorIndices, IndirectionTable, &IndirectionTableSize);
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    UINT16 HashSecretKeySize = 40;
    UINT32 RssConfigSize = sizeof(*RssConfig) + HashSecretKeySize + IndirectionTableSize;

    RssConfig.reset((XDP_RSS_CONFIGURATION *)AllocMem(RssConfigSize));
    TEST_NOT_NULL(RssConfig.get());
    XdpInitializeRssConfiguration(RssConfig.get(), RssConfigSize);
    RssConfig->HashSecretKeyOffset = sizeof(*RssConfig);
    RssConfig->IndirectionTableOffset = RssConfig->HashSecretKeyOffset + HashSecretKeySize;

    PROCESSOR_NUMBER *IndirectionTableDst =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(RssConfig.get(), RssConfig->IndirectionTableOffset);
    RtlCopyMemory(IndirectionTableDst, IndirectionTable.get(), IndirectionTableSize);

    RssConfig->Flags =
        XDP_RSS_FLAG_SET_HASH_TYPE | XDP_RSS_FLAG_SET_HASH_SECRET_KEY |
        XDP_RSS_FLAG_SET_INDIRECTION_TABLE;
    RssConfig->HashType = XDP_RSS_HASH_TYPE_TCP_IPV4 | XDP_RSS_HASH_TYPE_TCP_IPV6;
    RssConfig->HashSecretKeySize = HashSecretKeySize;
    RssConfig->IndirectionTableSize = (USHORT)IndirectionTableSize;

    RssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);

    //
    // Set an OID filter, start tearing down the NIC and filter binding, verify
    // the resulting OID, then close the handle to allow OID completion.
    //

    OID_KEY Key;
    InitializeOidKey(&Key, OID_GEN_RECEIVE_SCALE_PARAMETERS, NdisRequestSetInformation);
    MpOidFilter(AdapterMp, &Key, 1);

    HRESULT Result = E_FAIL;
    CxPlatAsyncT<HRESULT> Async([](HRESULT* Result) {
        *Result = FnMpIf.TryUnbindXdp();
    }, &Result);

    auto BindingScopeGuard = wil::scope_exit([&]
    {
        TEST_HRESULT(If.TryRebindXdp());
    });

    UINT32 OidInfoBufferLength;
    unique_malloc_ptr<VOID> OidInfoBuffer =
        MpOidAllocateAndGetRequest(AdapterMp, Key, &OidInfoBufferLength, MP_RESTART_TIMEOUT_MS);

    NDIS_RECEIVE_SCALE_PARAMETERS *NdisParams =
        (NDIS_RECEIVE_SCALE_PARAMETERS *)OidInfoBuffer.get();
    TEST_EQUAL(NdisParams->IndirectionTableSize, OriginalIndirectionTableSize);
    PROCESSOR_NUMBER *NdisIndirectionTable =
        (PROCESSOR_NUMBER *)RTL_PTR_ADD(NdisParams, NdisParams->IndirectionTableOffset);
    TEST_TRUE(
        RtlEqualMemory(NdisIndirectionTable,
        OriginalIndirectionTable.get(),
        OriginalIndirectionTableSize));

    AdapterMp.reset();
    TEST_TRUE(Async.WaitFor(TEST_TIMEOUT_ASYNC_MS));
    TEST_HRESULT(Result);
}

VOID
OffloadSetHardwareCapabilities()
{
    const auto &If = FnMpIf;
    auto DefaultLwf = LwfOpenDefault(If.GetIfIndex());
    auto RegistryScopeGuard = wil::scope_exit([&]
    {
        //
        // Reset all NIC advanced properties; without using undocumented APIs,
        // setting current configuration via OIDs is persistent.
        //
        If.Reset();
    });

    //
    // Send a private OID to the FNMP from FNLWF to update the advertised HW
    // capabilities. This should generate a status indication.
    //

    struct {
        UINT32 Oid;
        UINT32 Status;
    } Configs[] = {
        {
            OID_TCP_OFFLOAD_HW_PARAMETERS,
            NDIS_STATUS_TASK_OFFLOAD_HARDWARE_CAPABILITIES,
        },
        {
            OID_TCP_OFFLOAD_PARAMETERS,
            NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG,
        },
    };

    for (UINT32 i = 0; i < RTL_NUMBER_OF(Configs); i++) {
        LwfStatusSetFilter(DefaultLwf, Configs[i].Status, TRUE, TRUE);

        OID_KEY OidKey;
        NDIS_OFFLOAD_PARAMETERS OffloadParams;
        InitializeOffloadParameters(&OffloadParams);
        OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED;
        InitializeOidKey(&OidKey, Configs[i].Oid, NdisRequestSetInformation);
        UINT32 OidBufferSize = sizeof(OffloadParams);
        TEST_HRESULT(LwfOidSubmitRequest(DefaultLwf, OidKey, &OidBufferSize, &OffloadParams));

        UINT32 NdisOffloadSize;
        unique_malloc_ptr<NDIS_OFFLOAD> Offload =
            LwfStatusAllocateAndGetIndication<NDIS_OFFLOAD>(DefaultLwf, &NdisOffloadSize);

        TEST_TRUE(NdisOffloadSize >= NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1);
        TEST_TRUE(Offload->Header.Revision >= NDIS_OFFLOAD_REVISION_1);
        TEST_EQUAL(NDIS_OBJECT_TYPE_OFFLOAD, Offload->Header.Type);
        TEST_EQUAL(NDIS_OFFLOAD_SUPPORTED, Offload->Checksum.IPv4Transmit.UdpChecksum);
    }

    If.Reset();
    DefaultLwf = LwfOpenDefault(If.GetIfIndex());

    //
    // Set the HW capabilities and current config via registry, restart the NIC,
    // and verify the current config matches the registry.
    //

    OID_KEY OidKey;
    InitializeOidKey(&OidKey, OID_TCP_OFFLOAD_CURRENT_CONFIG, NdisRequestQueryInformation);
    UINT32 NdisOffloadSize;
    unique_malloc_ptr<NDIS_OFFLOAD> Offload =
        LwfOidAllocateAndSubmitRequest<NDIS_OFFLOAD>(DefaultLwf, OidKey, &NdisOffloadSize);

    TEST_TRUE(NdisOffloadSize >= NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1);
    TEST_TRUE(Offload->Header.Revision >= NDIS_OFFLOAD_REVISION_1);
    TEST_EQUAL(NDIS_OBJECT_TYPE_OFFLOAD, Offload->Header.Type);
    TEST_EQUAL(NDIS_OFFLOAD_NOT_SUPPORTED, Offload->Checksum.IPv4Transmit.UdpChecksum);

    CHAR CmdBuff[256];
    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(CmdBuff, "%s /c Set-NetAdapterAdvancedProperty -ifDesc \"%s\" -DisplayName UDPChecksumOffloadIPv4Capability -DisplayValue 'TX Enabled' -NoRestart", PowershellPrefix, If.GetIfDesc());
    TEST_EQUAL(0, InvokeSystem(CmdBuff));

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(CmdBuff, "%s /c Set-NetAdapterAdvancedProperty -ifDesc \"%s\" -DisplayName UDPChecksumOffloadIPv4 -DisplayValue 'TX Enabled' -NoRestart", PowershellPrefix, If.GetIfDesc());
    TEST_EQUAL(0, InvokeSystem(CmdBuff));

    If.Restart();

    DefaultLwf = LwfOpenDefault(If.GetIfIndex());

    Offload =
        LwfOidAllocateAndSubmitRequest<NDIS_OFFLOAD>(DefaultLwf, OidKey, &NdisOffloadSize);

    TEST_TRUE(NdisOffloadSize >= NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1);
    TEST_TRUE(Offload->Header.Revision >= NDIS_OFFLOAD_REVISION_1);
    TEST_EQUAL(NDIS_OBJECT_TYPE_OFFLOAD, Offload->Header.Type);
    TEST_EQUAL(NDIS_OFFLOAD_SUPPORTED, Offload->Checksum.IPv4Transmit.UdpChecksum);
}

VOID
GenericXskQueryAffinity()
{
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> RssConfig;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> ModifiedRssConfig;
    unique_malloc_ptr<XDP_RSS_CONFIGURATION> OriginalRssConfig;
    UCHAR BufferVa[] = "GenericXskQueryAffinity";
    auto GenericMp = MpOpenGeneric(FnMpIf.GetIfIndex());

    //
    // Only run if we have at least 2 LPs.
    // Our expected test automation environment is at least a 2VP VM.
    //
    if (GetProcessorCount() < 2) {
        TEST_WARNING("Test requires at least 2 logical processors. Skipping.");
        return;
    }

    for (auto Case : RxTxTestCases) {
        HRESULT Result;
        PROCESSOR_NUMBER ProcNumber;
        UINT32 ProcNumberSize;

        //
        // Bind socket (and set up RX program) on queue 0.
        //
        auto Socket =
            SetupSocket(
                FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);

        //
        // Verify that the initial affinity state is unknown.
        //

        if (Case.Rx) {
            UINT32 Enabled = TRUE;
            SetSockopt(
                Socket.Handle.get(), XSK_SOCKOPT_RX_PROCESSOR_AFFINITY, &Enabled,
                sizeof(Enabled));

            ProcNumberSize = sizeof(ProcNumber);
            Result =
                TryGetSockopt(
                    Socket.Handle.get(), XSK_SOCKOPT_RX_PROCESSOR_AFFINITY, &ProcNumber,
                    &ProcNumberSize);
            TEST_TRUE(FAILED(Result));
            TEST_FALSE(XskRingAffinityChanged(&Socket.Rings.Rx));
        }

        if (Case.Tx) {
            UINT32 Enabled = TRUE;
            SetSockopt(
                Socket.Handle.get(), XSK_SOCKOPT_TX_PROCESSOR_AFFINITY, &Enabled,
                sizeof(Enabled));

            ProcNumberSize = sizeof(ProcNumber);
            Result =
                TryGetSockopt(
                    Socket.Handle.get(), XSK_SOCKOPT_TX_PROCESSOR_AFFINITY, &ProcNumber,
                    &ProcNumberSize);
            TEST_TRUE(FAILED(Result));
            TEST_FALSE(XskRingAffinityChanged(&Socket.Rings.Tx));
        }

        //
        // Verify that activity on the data path updates the RSS state.
        //

        for (UINT32 ProcIndex = 0; ProcIndex < 2; ProcIndex++) {
            unique_malloc_ptr<PROCESSOR_NUMBER> IndirectionTable;
            wil::unique_handle InterfaceHandle;
            UINT32 IndirectionTableSize;

            PROCESSOR_NUMBER TargetProcNumber;
            ProcessorIndexToProcessorNumber(ProcIndex, &TargetProcNumber);

            CxPlatVector<UINT32> ProcessorIndices;
            TEST_TRUE(ProcessorIndices.push_back(ProcIndex));
            CreateIndirectionTable(ProcessorIndices, IndirectionTable, &IndirectionTableSize);
            InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
            SetXdpRss(FnMpIf, InterfaceHandle, IndirectionTable, IndirectionTableSize);

            if (Case.Rx) {
                TEST_FALSE(XskRingAffinityChanged(&Socket.Rings.Rx));

                SocketProduceRxFill(&Socket, 1);

                RX_FRAME Frame;
                RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), BufferVa, sizeof(BufferVa));
                TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));

                DATA_FLUSH_OPTIONS FlushOptions = {0};
                FlushOptions.Flags.RssCpu = TRUE;
                FlushOptions.RssCpuQueueId = FnMpIf.GetQueueId();
                TEST_HRESULT(TryMpRxFlush(GenericMp, &FlushOptions));

                SocketConsumerReserve(&Socket.Rings.Rx, 1);
                XskRingConsumerRelease(&Socket.Rings.Rx, 1);
                TEST_TRUE(XskRingAffinityChanged(&Socket.Rings.Rx));

                ProcNumberSize = sizeof(ProcNumber);
                GetSockopt(
                    Socket.Handle.get(), XSK_SOCKOPT_RX_PROCESSOR_AFFINITY, &ProcNumber,
                    &ProcNumberSize);
                TEST_EQUAL(sizeof(ProcNumber), ProcNumberSize);
                TEST_EQUAL(TargetProcNumber.Group, ProcNumber.Group);
                TEST_EQUAL(TargetProcNumber.Number, ProcNumber.Number);

                TEST_FALSE(XskRingAffinityChanged(&Socket.Rings.Rx));
            }

            if (Case.Tx) {
                TEST_FALSE(XskRingAffinityChanged(&Socket.Rings.Tx));

                UINT64 TxBuffer = SocketFreePop(&Socket);
                UCHAR *UdpFrame = Socket.Umem.Buffer.get() + TxBuffer;
                RtlCopyMemory(UdpFrame, BufferVa, sizeof(BufferVa));
                UINT32 ProducerIndex;
                TEST_EQUAL(1, XskRingProducerReserve(&Socket.Rings.Tx, 1, &ProducerIndex));
                XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Socket, ProducerIndex++);
                TxDesc->Address.AddressAndOffset = TxBuffer;
                TxDesc->Length = sizeof(BufferVa);
                XskRingProducerSubmit(&Socket.Rings.Tx, 1);
                XSK_NOTIFY_RESULT_FLAGS NotifyResult;
                NotifySocket(Socket.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
                TEST_EQUAL(0, NotifyResult);
                SocketConsumerReserve(&Socket.Rings.Completion, 1);
                XskRingConsumerRelease(&Socket.Rings.Completion, 1);

                TEST_TRUE(XskRingAffinityChanged(&Socket.Rings.Tx));

                ProcNumberSize = sizeof(ProcNumber);
                GetSockopt(
                    Socket.Handle.get(), XSK_SOCKOPT_TX_PROCESSOR_AFFINITY, &ProcNumber,
                    &ProcNumberSize);
                TEST_EQUAL(sizeof(ProcNumber), ProcNumberSize);
                TEST_EQUAL(TargetProcNumber.Group, ProcNumber.Group);
                TEST_EQUAL(TargetProcNumber.Number, ProcNumber.Number);

                TEST_FALSE(XskRingAffinityChanged(&Socket.Rings.Tx));
            }
        }
    }
}

static const struct {
    XDP_QUIC_OPERATION Xdp;
    NDIS_QUIC_OPERATION Ndis;
} QeoOperationMap[] = {
    {
        XDP_QUIC_OPERATION_ADD, NDIS_QUIC_OPERATION_ADD
    },
    {
        XDP_QUIC_OPERATION_REMOVE, NDIS_QUIC_OPERATION_REMOVE
    },
};

static const struct {
    XDP_QUIC_DIRECTION Xdp;
    NDIS_QUIC_DIRECTION Ndis;
} QeoDirectionMap[] = {
    {
        XDP_QUIC_DIRECTION_TRANSMIT, NDIS_QUIC_DIRECTION_TRANSMIT
    },
    {
        XDP_QUIC_DIRECTION_RECEIVE, NDIS_QUIC_DIRECTION_RECEIVE
    },
};

static const struct {
    XDP_QUIC_DECRYPT_FAILURE_ACTION Xdp;
    NDIS_QUIC_DECRYPT_FAILURE_ACTION Ndis;
} QeoDecryptFailureActionMap[] = {
    {
        XDP_QUIC_DECRYPT_FAILURE_ACTION_DROP, NDIS_QUIC_DECRYPT_FAILURE_ACTION_DROP
    },
    {
        XDP_QUIC_DECRYPT_FAILURE_ACTION_CONTINUE, NDIS_QUIC_DECRYPT_FAILURE_ACTION_CONTINUE
    },
};

static const struct {
    XDP_QUIC_CIPHER_TYPE Xdp;
    NDIS_QUIC_CIPHER_TYPE Ndis;
} QeoCipherTypeMap[] = {
    {
        XDP_QUIC_CIPHER_TYPE_AEAD_AES_128_GCM, NDIS_QUIC_CIPHER_TYPE_AEAD_AES_128_GCM
    },
    {
        XDP_QUIC_CIPHER_TYPE_AEAD_AES_256_GCM, NDIS_QUIC_CIPHER_TYPE_AEAD_AES_256_GCM
    },
    {
        XDP_QUIC_CIPHER_TYPE_AEAD_CHACHA20_POLY1305, NDIS_QUIC_CIPHER_TYPE_AEAD_CHACHA20_POLY1305
    },
    {
        XDP_QUIC_CIPHER_TYPE_AEAD_AES_128_CCM, NDIS_QUIC_CIPHER_TYPE_AEAD_AES_128_CCM
    },
};

static const struct {
    XDP_QUIC_ADDRESS_FAMILY Xdp;
    NDIS_QUIC_ADDRESS_FAMILY Ndis;
} QeoAddressFamilyMap[] = {
    {
        XDP_QUIC_ADDRESS_FAMILY_INET4, NDIS_QUIC_ADDRESS_FAMILY_INET4
    },
    {
        XDP_QUIC_ADDRESS_FAMILY_INET6, NDIS_QUIC_ADDRESS_FAMILY_INET6
    },
};

static const struct {
    NDIS_STATUS Ndis;
    HRESULT Xdp;
} QeoOffloadStatusMap[] = {
    {
        NDIS_STATUS_FAILURE, HRESULT_FROM_WIN32(ERROR_GEN_FAILURE)
    },
    {
        NDIS_STATUS_SUCCESS, S_OK
    },
};

static
NDIS_OID
OffloadQeoGetExpectedOid()
{
    return
        OsVersionIsGaOrLater() ?
            OID_QUIC_CONNECTION_ENCRYPTION : OID_QUIC_CONNECTION_ENCRYPTION_PROTOTYPE;
}

VOID
OffloadQeoConnection()
{
    for (const auto &Direction : QeoDirectionMap) {
    for (const auto &DecryptFailureAction : QeoDecryptFailureActionMap) {
    for (const auto &KeyPhase : {0U, 1U}) {
    for (const auto &CipherType : QeoCipherTypeMap) {
    for (const auto &AddressFamily : QeoAddressFamilyMap) {
        auto If = FnMpIf;
        auto InterfaceHandle = InterfaceOpen(If.GetIfIndex());

        for (const auto &Operation : QeoOperationMap) {
        for (const auto &OffloadStatus : QeoOffloadStatusMap) {
            auto AdapterMp = MpOpenAdapter(If.GetIfIndex());

            //
            // Initialize the connection for the add operation.
            //
            XDP_QUIC_CONNECTION Connection;
            XdpInitializeQuicConnection(&Connection, sizeof(Connection));
            Connection.Operation = Operation.Xdp;
            Connection.Direction = Direction.Xdp;
            Connection.DecryptFailureAction = DecryptFailureAction.Xdp;
            Connection.KeyPhase = KeyPhase;
            Connection.CipherType = CipherType.Xdp;
            Connection.AddressFamily = AddressFamily.Xdp;
            Connection.UdpPort = htons(1234);
            Connection.NextPacketNumber = 5678;
            Connection.ConnectionIdLength = 3;
            strcpy_s((CHAR *)Connection.Address, sizeof(Connection.Address), "Address");
            strcpy_s((CHAR *)Connection.ConnectionId, sizeof(Connection.ConnectionId), "Id");
            strcpy_s((CHAR *)Connection.PayloadKey, sizeof(Connection.PayloadKey), "PayloadKey");
            strcpy_s((CHAR *)Connection.HeaderKey, sizeof(Connection.HeaderKey), "HeaderKey");
            strcpy_s((CHAR *)Connection.PayloadIv, sizeof(Connection.PayloadIv), "PayloadIv");
            Connection.Status = E_FAIL;

            //
            // Configure the functional miniport to capture the offload request OID.
            //
            OID_KEY Key;
            InitializeOidKey(
                &Key, OffloadQeoGetExpectedOid(), NdisRequestMethod, OID_REQUEST_INTERFACE_DIRECT);
            MpOidFilter(AdapterMp, &Key, 1);

            //
            // Initiate the offload request on a separate thread: the operation will
            // block until the OID is completed, which won't happen until the miniport
            // handle is reset below.
            //
            struct TRY_QEO_SET_THREAD_CONTEXT {
                HANDLE InterfaceHandle;
                XDP_QUIC_CONNECTION *Connection;
                HRESULT Result;
            } Ctx;
            Ctx.InterfaceHandle = InterfaceHandle.get();
            Ctx.Connection = &Connection;

            CxPlatAsyncT<TRY_QEO_SET_THREAD_CONTEXT> Async([](TRY_QEO_SET_THREAD_CONTEXT *Ctx) {
                Ctx->Result = TryQeoSet(Ctx->InterfaceHandle, Ctx->Connection, sizeof(*Ctx->Connection));
            }, &Ctx);

            //
            // In case of failure, ensure the adapter is cleaned up before the
            // async thread destructor runs; otherwise a deadlock on the OID
            // path occurs.
            //
            auto AdapterScopeGuard = wil::scope_exit([&]
            {
                AdapterMp.reset();
            });

            //
            // Retrieve the captured offload OID from the functional miniport.
            //
            UINT32 OidInfoBufferLength;
            unique_malloc_ptr<VOID> OidInfoBuffer =
                MpOidAllocateAndGetRequest(AdapterMp, Key, &OidInfoBufferLength);


            //
            // Verify the OID parameters match the XDP connection request.
            //
            NDIS_QUIC_CONNECTION *NdisConnection = (NDIS_QUIC_CONNECTION *)OidInfoBuffer.get();

            TEST_EQUAL(sizeof(*NdisConnection), OidInfoBufferLength);

            TEST_EQUAL((UINT32)Operation.Ndis, NdisConnection->Operation);
            TEST_EQUAL((UINT32)Direction.Ndis, NdisConnection->Direction);
            TEST_EQUAL((UINT32)DecryptFailureAction.Ndis, NdisConnection->DecryptFailureAction);
            TEST_EQUAL(KeyPhase, NdisConnection->KeyPhase);
            TEST_EQUAL((UINT32)CipherType.Ndis, NdisConnection->CipherType);
            TEST_EQUAL(AddressFamily.Ndis, NdisConnection->AddressFamily);
            TEST_EQUAL(Connection.UdpPort, NdisConnection->UdpPort);
            TEST_EQUAL(Connection.NextPacketNumber, NdisConnection->NextPacketNumber);
            TEST_EQUAL(Connection.ConnectionIdLength, NdisConnection->ConnectionIdLength);

            C_ASSERT(sizeof(Connection.Address) == sizeof(NdisConnection->Address));
            TEST_TRUE(RtlEqualMemory(
                Connection.Address, NdisConnection->Address, sizeof(Connection.Address)));

            C_ASSERT(sizeof(Connection.ConnectionId) == sizeof(NdisConnection->ConnectionId));
            TEST_TRUE(RtlEqualMemory(
                Connection.ConnectionId, NdisConnection->ConnectionId, Connection.ConnectionIdLength));

            C_ASSERT(sizeof(Connection.PayloadKey) == sizeof(NdisConnection->PayloadKey));
            TEST_TRUE(RtlEqualMemory(
                Connection.PayloadKey, NdisConnection->PayloadKey, sizeof(Connection.PayloadKey)));

            C_ASSERT(sizeof(Connection.HeaderKey) == sizeof(NdisConnection->HeaderKey));
            TEST_TRUE(RtlEqualMemory(
                Connection.HeaderKey, NdisConnection->HeaderKey, sizeof(Connection.HeaderKey)));

            C_ASSERT(sizeof(Connection.PayloadIv) == sizeof(NdisConnection->PayloadIv));
            TEST_TRUE(RtlEqualMemory(
                Connection.PayloadIv, NdisConnection->PayloadIv, sizeof(Connection.PayloadIv)));

            //
            // Complete the OID with the connection offload status updated.
            //
            NdisConnection->Status = OffloadStatus.Ndis;

            MpOidCompleteRequest(
                AdapterMp, Key, NDIS_STATUS_SUCCESS, NdisConnection, sizeof(*NdisConnection));

            //
            // Verify the XDP offload API is completed once the OID completes.
            //
            TEST_TRUE(Async.WaitFor(TEST_TIMEOUT_ASYNC_MS));

            //
            // Verify the XDP offload API succeeded.
            //
            TEST_HRESULT(Ctx.Result);

            //
            // Verify the connection's status field was updated with the miniport
            // status.
            //
            TEST_EQUAL(OffloadStatus.Xdp, Connection.Status);

            if (SUCCEEDED(OffloadStatus.Xdp)) {
                //
                // Verify duplicate connections cannot be added, and non-existent
                // entries cannot be removed.
                //
                if (Operation.Xdp == XDP_QUIC_OPERATION_ADD) {
                    TEST_EQUAL(
                        HRESULT_FROM_WIN32(ERROR_OBJECT_ALREADY_EXISTS),
                        TryQeoSet(InterfaceHandle.get(), &Connection, sizeof(Connection)));
                } else {
                    ASSERT(Operation.Xdp == XDP_QUIC_OPERATION_REMOVE);
                    TEST_EQUAL(
                        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                        TryQeoSet(InterfaceHandle.get(), &Connection, sizeof(Connection)));
                }
            }
        }}}}}}
    }
}

VOID
OffloadQeoRevert(
    _In_ REVERT_REASON RevertReason
    )
{
    auto If = FnMpIf;
    auto InterfaceHandle = InterfaceOpen(If.GetIfIndex());
    auto AdapterMp = MpOpenAdapter(If.GetIfIndex());
    const auto Timeout =
        RevertReason == RevertReasonInterfaceRemoval ?
            MP_RESTART_TIMEOUT_MS : TEST_TIMEOUT_ASYNC_MS;

    const auto &Operation = QeoOperationMap[0];
    const auto &Direction = QeoDirectionMap[0];
    const auto &DecryptFailureAction = QeoDecryptFailureActionMap[0];
    const auto &KeyPhase = 0U;
    const auto &CipherType = QeoCipherTypeMap[0];
    const auto &AddressFamily = QeoAddressFamilyMap[0];

    ASSERT(Operation.Xdp == XDP_QUIC_OPERATION_ADD);

    //
    // Initialize the connection for the add operation.
    //
    XDP_QUIC_CONNECTION Connection;
    XdpInitializeQuicConnection(&Connection, sizeof(Connection));
    Connection.Operation = Operation.Xdp;
    Connection.Direction = Direction.Xdp;
    Connection.DecryptFailureAction = DecryptFailureAction.Xdp;
    Connection.KeyPhase = KeyPhase;
    Connection.CipherType = CipherType.Xdp;
    Connection.AddressFamily = AddressFamily.Xdp;
    Connection.UdpPort = htons(1234);
    Connection.NextPacketNumber = 5678;
    Connection.ConnectionIdLength = 3;
    strcpy_s((CHAR *)Connection.Address, sizeof(Connection.Address), "Address");
    strcpy_s((CHAR *)Connection.ConnectionId, sizeof(Connection.ConnectionId), "Id");
    strcpy_s((CHAR *)Connection.PayloadKey, sizeof(Connection.PayloadKey), "PayloadKey");
    strcpy_s((CHAR *)Connection.HeaderKey, sizeof(Connection.HeaderKey), "HeaderKey");
    strcpy_s((CHAR *)Connection.PayloadIv, sizeof(Connection.PayloadIv), "PayloadIv");
    Connection.Status = E_FAIL;

    TEST_HRESULT(TryQeoSet(InterfaceHandle.get(), &Connection, sizeof(Connection)));

    //
    // Configure the functional miniport to capture the offload request OID.
    //
    OID_KEY Key;
    InitializeOidKey(
        &Key, OffloadQeoGetExpectedOid(), NdisRequestMethod, OID_REQUEST_INTERFACE_DIRECT);
    MpOidFilter(AdapterMp, &Key, 1);

    //
    // Initiate the offload request on a separate thread: the operation will
    // block until the OID is completed, which won't happen until the miniport
    // handle is reset below.
    //
    struct QEO_REVERT_THREAD_CONTEXT {
        wil::unique_handle InterfaceHandle;
        REVERT_REASON RevertReason;
        bool Succeeded;
    } Ctx;
    Ctx.InterfaceHandle.reset(InterfaceHandle.release());
    Ctx.RevertReason = RevertReason;
    Ctx.Succeeded = FALSE;

    CxPlatAsyncT<QEO_REVERT_THREAD_CONTEXT> Async([](QEO_REVERT_THREAD_CONTEXT *Ctx)  {
        if (Ctx->RevertReason == RevertReasonInterfaceRemoval) {
            Ctx->Succeeded = FnMpIf.TryRestart();
        } else {
            ASSERT(Ctx->RevertReason == RevertReasonHandleClosure);
            Ctx->InterfaceHandle.reset();
            Ctx->Succeeded = true;
        }
    }, &Ctx);

    //
    // In case of failure, ensure the adapter is cleaned up before the
    // async thread destructor runs; otherwise a deadlock on the OID
    // path occurs.
    //
    auto AdapterScopeGuard = wil::scope_exit([&]
    {
        AdapterMp.reset();
    });

    //
    // Retrieve the captured offload OID from the functional miniport.
    //
    UINT32 OidInfoBufferLength;
    unique_malloc_ptr<VOID> OidInfoBuffer =
        MpOidAllocateAndGetRequest(AdapterMp, Key, &OidInfoBufferLength, Timeout);

    //
    // Verify the OID parameters match the XDP connection request.
    //
    NDIS_QUIC_CONNECTION *NdisConnection = (NDIS_QUIC_CONNECTION *)OidInfoBuffer.get();

    TEST_EQUAL(sizeof(*NdisConnection), OidInfoBufferLength);

    TEST_EQUAL((UINT32)NDIS_QUIC_OPERATION_REMOVE, NdisConnection->Operation);
    TEST_EQUAL((UINT32)Direction.Ndis, NdisConnection->Direction);
    TEST_EQUAL((UINT32)DecryptFailureAction.Ndis, NdisConnection->DecryptFailureAction);
    TEST_EQUAL(KeyPhase, NdisConnection->KeyPhase);
    TEST_EQUAL((UINT32)CipherType.Ndis, NdisConnection->CipherType);
    TEST_EQUAL(AddressFamily.Ndis, NdisConnection->AddressFamily);
    TEST_EQUAL(Connection.UdpPort, NdisConnection->UdpPort);
    TEST_EQUAL(Connection.NextPacketNumber, NdisConnection->NextPacketNumber);
    TEST_EQUAL(Connection.ConnectionIdLength, NdisConnection->ConnectionIdLength);

    C_ASSERT(sizeof(Connection.Address) == sizeof(NdisConnection->Address));
    TEST_TRUE(RtlEqualMemory(
        Connection.Address, NdisConnection->Address, sizeof(Connection.Address)));

    C_ASSERT(sizeof(Connection.ConnectionId) == sizeof(NdisConnection->ConnectionId));
    TEST_TRUE(RtlEqualMemory(
        Connection.ConnectionId, NdisConnection->ConnectionId, Connection.ConnectionIdLength));

    C_ASSERT(sizeof(Connection.PayloadKey) == sizeof(NdisConnection->PayloadKey));
    TEST_TRUE(RtlEqualMemory(
        Connection.PayloadKey, NdisConnection->PayloadKey, sizeof(Connection.PayloadKey)));

    C_ASSERT(sizeof(Connection.HeaderKey) == sizeof(NdisConnection->HeaderKey));
    TEST_TRUE(RtlEqualMemory(
        Connection.HeaderKey, NdisConnection->HeaderKey, sizeof(Connection.HeaderKey)));

    C_ASSERT(sizeof(Connection.PayloadIv) == sizeof(NdisConnection->PayloadIv));
    TEST_TRUE(RtlEqualMemory(
        Connection.PayloadIv, NdisConnection->PayloadIv, sizeof(Connection.PayloadIv)));

    //
    // Reset the miniport handle, allowing the captured OID to be completed.
    //
    AdapterMp.reset();

    //
    // Verify the revert/teardown is completed once the OID completes.
    //
    TEST_TRUE(Async.WaitFor(Timeout));

    //
    // Verify the revert/teardown succeeded.
    //
    TEST_TRUE(Ctx.Succeeded);
}

VOID
OffloadQeoOidFailure(
    )
{
    auto If = FnMpIf;
    auto InterfaceHandle = InterfaceOpen(If.GetIfIndex());
    auto AdapterMp = MpOpenAdapter(If.GetIfIndex());

    const auto &Operation = QeoOperationMap[0];
    const auto &Direction = QeoDirectionMap[0];
    const auto &DecryptFailureAction = QeoDecryptFailureActionMap[0];
    const auto &KeyPhase = 0U;
    const auto &CipherType = QeoCipherTypeMap[0];
    const auto &AddressFamily = QeoAddressFamilyMap[0];

    ASSERT(Operation.Xdp == XDP_QUIC_OPERATION_ADD);

    //
    // Initialize the connection for the add operation.
    //
    XDP_QUIC_CONNECTION Connection;
    XdpInitializeQuicConnection(&Connection, sizeof(Connection));
    Connection.Operation = Operation.Xdp;
    Connection.Direction = Direction.Xdp;
    Connection.DecryptFailureAction = DecryptFailureAction.Xdp;
    Connection.KeyPhase = KeyPhase;
    Connection.CipherType = CipherType.Xdp;
    Connection.AddressFamily = AddressFamily.Xdp;
    Connection.UdpPort = htons(1234);
    Connection.NextPacketNumber = 5678;
    Connection.ConnectionIdLength = 3;
    strcpy_s((CHAR *)Connection.Address, sizeof(Connection.Address), "Address");
    strcpy_s((CHAR *)Connection.ConnectionId, sizeof(Connection.ConnectionId), "Id");
    strcpy_s((CHAR *)Connection.PayloadKey, sizeof(Connection.PayloadKey), "PayloadKey");
    strcpy_s((CHAR *)Connection.HeaderKey, sizeof(Connection.HeaderKey), "HeaderKey");
    strcpy_s((CHAR *)Connection.PayloadIv, sizeof(Connection.PayloadIv), "PayloadIv");
    Connection.Status = E_FAIL;

    //
    // Configure the functional miniport to capture the offload request OID.
    //
    OID_KEY Key;
    InitializeOidKey(
        &Key, OffloadQeoGetExpectedOid(), NdisRequestMethod, OID_REQUEST_INTERFACE_DIRECT);
    MpOidFilter(AdapterMp, &Key, 1);

    //
    // Initiate the offload request on a separate thread: the operation will
    // block until the OID is completed, which won't happen until the miniport
    // handle is reset below.
    //
    struct TRY_QEO_SET_THREAD_CONTEXT {
        HANDLE InterfaceHandle;
        XDP_QUIC_CONNECTION *Connection;
        HRESULT Result;
    } Ctx;
    Ctx.InterfaceHandle = InterfaceHandle.get();
    Ctx.Connection = &Connection;

    CxPlatAsyncT<TRY_QEO_SET_THREAD_CONTEXT> Async([](TRY_QEO_SET_THREAD_CONTEXT *Ctx) {
        Ctx->Result = TryQeoSet(Ctx->InterfaceHandle, Ctx->Connection, sizeof(*Ctx->Connection));
    }, &Ctx);

    //
    // In case of failure, ensure the adapter is cleaned up before the
    // async thread destructor runs; otherwise a deadlock on the OID
    // path occurs.
    //
    auto AdapterScopeGuard = wil::scope_exit([&]
    {
        AdapterMp.reset();
    });

    //
    // Retrieve the captured offload OID from the functional miniport.
    //
    UINT32 OidInfoBufferLength;
    unique_malloc_ptr<VOID> OidInfoBuffer =
        MpOidAllocateAndGetRequest(AdapterMp, Key, &OidInfoBufferLength);

    NDIS_QUIC_CONNECTION *NdisConnection = (NDIS_QUIC_CONNECTION *)OidInfoBuffer.get();

    TEST_EQUAL(sizeof(*NdisConnection), OidInfoBufferLength);
    TEST_EQUAL(NDIS_STATUS_PENDING, NdisConnection->Status);

    MpOidCompleteRequest(AdapterMp, Key, NDIS_STATUS_FAILURE, NdisConnection, OidInfoBufferLength);

    //
    // Verify the XDP offload API is completed once the OID completes.
    //
    TEST_TRUE(Async.WaitFor(TEST_TIMEOUT_ASYNC_MS));

    //
    // Verify the XDP offload API failed.
    //
    TEST_TRUE(FAILED(Ctx.Result));
}

VOID
OidPassthru()
{
    typedef struct _OID_PARAMS {
        OID_KEY Key;
        UINT32 BufferSize;
        UINT32 CompletionSize;
    } OID_PARAMS;
    OID_PARAMS OidKeys[3] = {0};
    UINT32 MpInfoBufferLength;
    unique_malloc_ptr<VOID> MpInfoBuffer;
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    TEST_NOT_NULL(DefaultLwf.get());

    //
    // Get.
    //
    OidKeys[0].BufferSize = sizeof(ULONG);
    OidKeys[0].CompletionSize = sizeof(ULONG);
    InitializeOidKey(&OidKeys[0].Key, OID_GEN_RECEIVE_BLOCK_SIZE, NdisRequestQueryInformation);

    //
    // Set.
    //
    OidKeys[1].BufferSize = sizeof(ULONG);
    OidKeys[1].CompletionSize = sizeof(ULONG);
    InitializeOidKey(&OidKeys[1].Key, OID_GEN_CURRENT_PACKET_FILTER, NdisRequestSetInformation);

    //
    // Method. (Direct OID)
    //
    OidKeys[2].BufferSize = sizeof(NDIS_QUIC_CONNECTION);
    OidKeys[2].CompletionSize = sizeof(NDIS_QUIC_CONNECTION);
    InitializeOidKey(
        &OidKeys[2].Key, OID_QUIC_CONNECTION_ENCRYPTION_PROTOTYPE, NdisRequestMethod,
        OID_REQUEST_INTERFACE_DIRECT);

    //
    // Verify synchronous OID completion, i.e. without FNMP pending the OID.
    //
    for (UINT32 Index = 0; Index < RTL_NUMBER_OF(OidKeys); Index++) {
        const OID_PARAMS *OidParam = &OidKeys[Index];
        UINT32 LwfInfoBufferLength = OidParam->BufferSize;
        unique_malloc_ptr<UCHAR> LwfInfoBuffer((UCHAR *)AllocMem(LwfInfoBufferLength));
        TEST_NOT_NULL(LwfInfoBuffer.get());

        TEST_HRESULT(LwfOidSubmitRequest(
            DefaultLwf, OidParam->Key, &LwfInfoBufferLength, LwfInfoBuffer.get()));
        TEST_EQUAL(LwfInfoBufferLength, OidParam->CompletionSize);
    }

    //
    // Verify asynchronous completions.
    //

    for (UINT32 Index = 0; Index < RTL_NUMBER_OF(OidKeys); Index++) {
        const OID_PARAMS *OidParam = &OidKeys[Index];
        UINT32 LwfInfoBufferLength = OidParam->BufferSize;
        unique_malloc_ptr<UCHAR> LwfInfoBuffer((UCHAR *)AllocMem(LwfInfoBufferLength));
        TEST_NOT_NULL(LwfInfoBuffer.get());
        const UINT32 CompletionSize = LwfInfoBufferLength / 2;

        auto ExclusiveMp = MpOpenAdapter(FnMpIf.GetIfIndex());
        TEST_NOT_NULL(ExclusiveMp.get());

        if (OidParam->Key.Oid == OID_GEN_CURRENT_PACKET_FILTER &&
            OidParam->Key.RequestType == NdisRequestSetInformation) {

            //
            // NDIS absorbs the set OID unless the packet filter is changed.
            // Query the current filter and then modify the info buffer.
            //
            OID_KEY GetKey = OidParam->Key;
            GetKey.RequestType = NdisRequestQueryInformation;
            TEST_HRESULT(LwfOidSubmitRequest(
                DefaultLwf, GetKey, &LwfInfoBufferLength, LwfInfoBuffer.get()));
            TEST_EQUAL(LwfInfoBufferLength, OidParam->CompletionSize);

            LwfInfoBuffer.get()[0] ^= 1;
        }

        MpOidFilter(ExclusiveMp, &OidParam->Key, 1);
        struct OID_REQUEST_THREAD_CONTEXT {
            unique_fnlwf_handle Handle;
            OID_KEY Key;
            UINT32 InformationBufferLength;
            VOID *InformationBuffer;
            HRESULT Result;
        } Ctx;
        Ctx.Handle.reset(DefaultLwf.release());
        Ctx.Key = OidParam->Key;
        Ctx.InformationBufferLength = LwfInfoBufferLength;
        Ctx.InformationBuffer = LwfInfoBuffer.get();

        CxPlatAsyncT<OID_REQUEST_THREAD_CONTEXT> Async([](OID_REQUEST_THREAD_CONTEXT *Ctx) {
            Ctx->Result =
                LwfOidSubmitRequest(
                    Ctx->Handle, Ctx->Key, &Ctx->InformationBufferLength, Ctx->InformationBuffer);
        }, &Ctx);

        MpInfoBuffer =
            MpOidAllocateAndGetRequest(ExclusiveMp, OidParam->Key, &MpInfoBufferLength);
        TEST_NOT_NULL(MpInfoBuffer.get());

        TEST_HRESULT(MpOidCompleteRequest(
            ExclusiveMp, OidParam->Key, STATUS_SUCCESS, LwfInfoBuffer.get(), CompletionSize));
        TEST_TRUE(Async.WaitFor(TEST_TIMEOUT_ASYNC_MS));
        TEST_HRESULT(Ctx.Result);

        DefaultLwf.reset(Ctx.Handle.release());
        TEST_EQUAL(Ctx.InformationBufferLength, CompletionSize);
    }
}

/**
 * TODO:
 *
 * RX program test cases:
 * - RX frames with fragmented headers have action performed.
 * - RX program can be detached and re-attached while XSK handle is open.
 * - RX program performs L2FWD and returns XDP_RX_ACTION_TX.
 *
 * Generic RX test cases:
 * - RX frames with more than 256 MDLs have action performed.
 * - RX frames larger than 64K have action performed.
 * - RX frames indicated with low resources flag.
 * - RX frames indicated at passive/dispatch level.
 * - RX frames indicated without RSS hash.
 * - RX frames indicated on unexpected CPU index.
 *
 * AF_XDP test cases:
 * - RX/TX wait is abandoned when thread terminates.
 * - RX/TX statistics are updated after drop/invalid descriptor/truncation.
 *
 * Binding test test cases:
 * - NIC can deregister native XDP while XSK handle is open.
 * - NMR itself can tear down binding between XDP and NIC.
 *
 */
