//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma warning(disable:26495)  // Always initialize a variable
#pragma warning(disable:26812)  // The enum type '_XDP_MODE' is unscoped.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <vector>

// Windows and WIL includes need to be ordered in a certain way.
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#pragma warning(push)
#pragma warning(disable:26457) // (void) should not be used to ignore return values, use 'std::ignore =' instead (es.48)
#include <wil/resource.h>
#pragma warning(pop)

#include <afxdp_helper.h>
#include <xdpapi.h>
#include <pkthlp.h>
#include <xdpfnmpapi.h>
#include <xdpfnlwfapi.h>

#include "xdptest.h"
#include "tests.h"
#include "trace.h"
#include "util.h"

#include "tests.tmh"

#define FNMP_IF_DESC "XDPFNMP"
#define FNMP_IPV4_ADDRESS "192.168.200.1"
#define FNMP_IPV6_ADDRESS "fc00::200:1"

#define FNMP1Q_IF_DESC "XDPFNMP #2"
#define FNMP1Q_IPV4_ADDRESS "192.168.201.1"
#define FNMP1Q_IPV6_ADDRESS "fc00::201:1"

#define DEFAULT_UMEM_SIZE 65536
#define DEFAULT_UMEM_CHUNK_SIZE 4096
#define DEFAULT_UMEM_HEADROOM 0
#define DEFAULT_RING_SIZE (DEFAULT_UMEM_SIZE / DEFAULT_UMEM_CHUNK_SIZE)

static CONST XDP_HOOK_ID XdpInspectRxL2 =
{
    XDP_HOOK_L2,
    XDP_HOOK_RX,
    XDP_HOOK_INSPECT,
};

//
// A timeout value that allows for a little latency, e.g. async threads to
// execute.
//
#define TEST_TIMEOUT_ASYNC_MS 1000
#define TEST_TIMEOUT_ASYNC std::chrono::milliseconds(TEST_TIMEOUT_ASYNC_MS)

//
// The expected maximum time needed for a network adapter to restart.
//
#define MP_RESTART_TIMEOUT std::chrono::seconds(10)

//
// Interval between polling attempts.
//
#define POLL_INTERVAL_MS 10
C_ASSERT(POLL_INTERVAL_MS * 5 <= TEST_TIMEOUT_ASYNC_MS);
C_ASSERT(POLL_INTERVAL_MS * 5 <= std::chrono::milliseconds(MP_RESTART_TIMEOUT).count());

template <typename T>
using unique_malloc_ptr = wistd::unique_ptr<T, wil::function_deleter<decltype(&::free), ::free>>;

using unique_xdp_api = wistd::unique_ptr<const XDP_API_TABLE, wil::function_deleter<decltype(&::XdpCloseApi), ::XdpCloseApi>>;

static unique_xdp_api XdpApi;

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
    UINT32 QueueId;
    MY_UMEM Umem;
    wil::unique_handle Handle;
    wil::unique_handle RxProgram;
    RING_SET Rings;
    std::stack<UINT64> FreeDescriptors;
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

static CONST CHAR *PowershellPrefix;

//
// Helper functions.
//

class TestInterface;

static
VOID
WaitForNdisDatapath(
    _In_ const TestInterface& If
    );

typedef NTSTATUS (WINAPI* RTL_GET_VERSION_FN)(PRTL_OSVERSIONINFOW);

RTL_OSVERSIONINFOW
GetOSVersion(
    )
{
    HMODULE Module = GetModuleHandleW(L"ntdll.dll");
    TEST_TRUE(Module != NULL);

    RTL_GET_VERSION_FN RtlGetVersion = (RTL_GET_VERSION_FN)GetProcAddress(Module, "RtlGetVersion");
    TEST_TRUE(RtlGetVersion != NULL);

    RTL_OSVERSIONINFOW OsVersion = { 0 };
    OsVersion.dwOSVersionInfoSize = sizeof(OsVersion);
    TEST_EQUAL(STATUS_SUCCESS, RtlGetVersion(&OsVersion));

    return OsVersion;
}

BOOLEAN
OsVersionIsFeOrLater(
    _In_ const RTL_OSVERSIONINFOW *OsVersion
    )
{
    return
        (OsVersion->dwMajorVersion > 10 || (OsVersion->dwMajorVersion == 10 &&
        (OsVersion->dwMinorVersion > 0 || (OsVersion->dwMinorVersion == 0 &&
        (OsVersion->dwBuildNumber >= 20100)))));
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

template<class T>
class Stopwatch {
private:
    LARGE_INTEGER _StartQpc;
    LARGE_INTEGER _FrequencyQpc;
    T _TimeoutInterval;

public:
    Stopwatch(
        _In_opt_ T TimeoutInterval = T::max()
        )
        :
        _TimeoutInterval(TimeoutInterval)
    {
        QueryPerformanceFrequency(&_FrequencyQpc);
        QueryPerformanceCounter(&_StartQpc);
    }

    T
    Elapsed()
    {
        LARGE_INTEGER End;
        UINT64 ElapsedQpc;

        QueryPerformanceCounter(&End);
        ElapsedQpc = End.QuadPart - _StartQpc.QuadPart;

        return T((ElapsedQpc * T::period::den) / T::period::num / _FrequencyQpc.QuadPart);
    }

    bool
    IsExpired()
    {
        return Elapsed() >= _TimeoutInterval;
    }

    void
    ExpectElapsed(
        _In_ T ExpectedInterval,
        _In_opt_ UINT32 MarginPercent = 10
        )
    {
        T Fudge = (ExpectedInterval * MarginPercent) / 100;
        TEST_TRUE(MarginPercent == 0 || Fudge > T(0));
        TEST_TRUE(Elapsed() >= ExpectedInterval - Fudge);
        TEST_TRUE(Elapsed() <= ExpectedInterval + Fudge);
    }

    void
    Reset()
    {
        QueryPerformanceCounter(&_StartQpc);
    }

    void
    Reset(
        _In_ T TimeoutInterval
        )
    {
        _TimeoutInterval = TimeoutInterval;
        Reset();
    }
};

class TestInterface {
private:
    CONST CHAR *_IfDesc;
    mutable UINT32 _IfIndex;
    mutable UCHAR _HwAddress[ETHERNET_MAC_SIZE]{ 0 };
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
        unique_malloc_ptr<IP_ADAPTER_INFO> AdapterInfoList{ (IP_ADAPTER_INFO *)malloc(OutBufLen) };
        TEST_NOT_NULL(AdapterInfoList);
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
        _In_z_ CONST CHAR *IfDesc,
        _In_z_ CONST CHAR *Ipv4Address,
        _In_z_ CONST CHAR *Ipv6Address
        )
        :
        _IfDesc(IfDesc),
        _IfIndex(NET_IFINDEX_UNSPECIFIED)
    {
        CONST CHAR *Terminator;
        TEST_NTSTATUS(RtlIpv4StringToAddressA(Ipv4Address, FALSE, &Terminator, &_Ipv4Address));
        TEST_NTSTATUS(RtlIpv6StringToAddressA(Ipv6Address, &Terminator, &_Ipv6Address));
    }

    CONST CHAR*
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
        HwAddress->Bytes[sizeof(_HwAddress) - 1]++;
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

    VOID
    Restart(BOOLEAN WaitForUp = TRUE) const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(CmdBuff, "%s /c Restart-NetAdapter -ifDesc \"%s\"", PowershellPrefix, _IfDesc);
        TEST_EQUAL(0, system(CmdBuff));

        if (WaitForUp) {
            WaitForNdisDatapath(*this);
        }
    }

    VOID
    Reset() const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(CmdBuff, "%s /c Reset-NetAdapterAdvancedProperty -ifDesc \"%s\" -DisplayName * -NoRestart", PowershellPrefix, _IfDesc);
        TEST_EQUAL(0, system(CmdBuff));
        Restart();
    }
};

static TestInterface FnMpIf(FNMP_IF_DESC, FNMP_IPV4_ADDRESS, FNMP_IPV6_ADDRESS);
static TestInterface FnMp1QIf(FNMP1Q_IF_DESC, FNMP1Q_IPV4_ADDRESS, FNMP1Q_IPV6_ADDRESS);

static
HRESULT
TryOpenApi(
    _Out_ unique_xdp_api &XdpApiTable,
    _In_ UINT32 Version = XDP_VERSION_PRERELEASE
    )
{
    return XdpOpenApi(Version, wil::out_param(XdpApiTable));
}

static
unique_xdp_api
OpenApi(
    _In_ UINT32 Version = XDP_VERSION_PRERELEASE
    )
{
    unique_xdp_api XdpApiTable;
    TEST_HRESULT(TryOpenApi(XdpApiTable, Version));
    return XdpApiTable;
}

static
wil::unique_handle
CreateSocket()
{
    wil::unique_handle Socket;
    TEST_HRESULT(XdpApi->XskCreate(&Socket));
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
    UmemRegistration->totalSize = DEFAULT_UMEM_SIZE;
    UmemRegistration->chunkSize = DEFAULT_UMEM_CHUNK_SIZE;
    UmemRegistration->headroom = DEFAULT_UMEM_HEADROOM;
    UmemRegistration->address = UmemBuffer;
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
    return XdpApi->XskGetSockopt(Socket, OptionName, OptionValue, OptionLength);
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
    return XdpApi->XskSetSockopt(Socket, OptionName, OptionValue, OptionLength);
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
    TEST_EQUAL(RingSize, InfoSet.fill.size);
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
    TEST_EQUAL(RingSize, InfoSet.completion.size);
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
    TEST_EQUAL(RingSize, InfoSet.rx.size);
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
    TEST_EQUAL(RingSize, InfoSet.tx.size);
}

static
VOID
SetRxHookId(
    _In_ HANDLE Socket,
    _In_ CONST XDP_HOOK_ID *HookId
    )
{
    SetSockopt(Socket, XSK_SOCKOPT_RX_HOOK_ID, HookId, sizeof(*HookId));
}

static
VOID
SetTxHookId(
    _In_ HANDLE Socket,
    _In_ CONST XDP_HOOK_ID *HookId
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
    return XdpApi->XskNotifySocket(Socket, Flags, WaitTimeoutMilliseconds, Result);
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
    return XdpApi->XskNotifyAsync(Socket, Flags, Overlapped);
}

static
HRESULT
TryGetNotifyAsyncResult(
    _In_ OVERLAPPED *Overlapped,
    _Out_ XSK_NOTIFY_RESULT_FLAGS *Result
    )
{
    return XdpApi->XskGetNotifyAsyncResult(Overlapped, Result);
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
    return XdpApi->XdpInterfaceOpen(InterfaceIndex, &InterfaceHandle);
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
    return XdpApi->XdpRssGetCapabilities(InterfaceHandle, RssCapabilities, RssCapabilitiesSize);
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
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
    _In_ UINT32 RssConfigurationSize
    )
{
    return XdpApi->XdpRssSet(InterfaceHandle, RssConfiguration, RssConfigurationSize);
}

static
VOID
RssSet(
    _In_ HANDLE InterfaceHandle,
    _In_ CONST XDP_RSS_CONFIGURATION *RssConfiguration,
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
    return XdpApi->XdpRssGet(InterfaceHandle, RssConfiguration, RssConfigurationSize);
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
TryCreateXdpProg(
    _Out_ wil::unique_handle &ProgramHandle,
    _In_ UINT32 IfIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_MODE XdpMode,
    _In_ XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _In_ UINT32 Flags = 0
    )
{
    ASSERT(Flags & (XDP_CREATE_PROGRAM_FLAG_GENERIC | XDP_CREATE_PROGRAM_FLAG_NATIVE) == 0);

    if (XdpMode == XDP_GENERIC) {
        Flags |= XDP_CREATE_PROGRAM_FLAG_GENERIC;
    } else if (XdpMode == XDP_NATIVE) {
        Flags |= XDP_CREATE_PROGRAM_FLAG_NATIVE;
    }

    return
        XdpApi->XdpCreateProgram(IfIndex, HookId, QueueId, Flags, Rules, RuleCount, &ProgramHandle);
}

static
wil::unique_handle
CreateXdpProg(
    _In_ UINT32 IfIndex,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _In_ XDP_MODE XdpMode,
    _In_ XDP_RULE *Rules,
    _In_ UINT32 RuleCount,
    _In_ UINT32 Flags = 0
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
    _In_ CONST XDP_HOOK_ID *HookId,
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
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx,
    _In_opt_ CONST XDP_HOOK_ID *RxHookId = nullptr,
    _In_opt_ CONST XDP_HOOK_ID *TxHookId = nullptr
    )
{
    Socket->Umem.Buffer = AllocUmemBuffer();
    InitUmem(&Socket->Umem.Reg, Socket->Umem.Buffer.get());
    SetUmem(Socket->Handle.get(), &Socket->Umem.Reg);

    SetFillRing(Socket->Handle.get());
    SetCompletionRing(Socket->Handle.get());

    if (Rx) {
        SetRxRing(Socket->Handle.get());
    }

    if (Tx) {
        SetTxRing(Socket->Handle.get());
    }

    if (RxHookId != nullptr) {
        SetRxHookId(Socket->Handle.get(), RxHookId);
    }
    if (TxHookId != nullptr) {
        SetTxHookId(Socket->Handle.get(), TxHookId);
    }
}

static
VOID
XskSetupPostBind(
    _Inout_ MY_SOCKET *Socket,
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    )
{
    XSK_RING_INFO_SET InfoSet;

    GetRingInfo(Socket->Handle.get(), &InfoSet);
    XskRingInitialize(&Socket->Rings.Fill, &InfoSet.fill);
    XskRingInitialize(&Socket->Rings.Completion, &InfoSet.completion);

    if (Rx) {
        XskRingInitialize(&Socket->Rings.Rx, &InfoSet.rx);
    }

    if (Tx) {
        XskRingInitialize(&Socket->Rings.Tx, &InfoSet.tx);
    }

    UINT64 BufferCount = Socket->Umem.Reg.totalSize / Socket->Umem.Reg.chunkSize;
    UINT64 Offset = 0;
    while (BufferCount-- > 0) {
        Socket->FreeDescriptors.push(Offset);
        Offset += Socket->Umem.Reg.chunkSize;
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
    CONST XDP_HOOK_ID *RxHookId = nullptr,
    CONST XDP_HOOK_ID *TxHookId = nullptr
    )
{
    MY_SOCKET Socket;

    Socket.Handle = CreateSocket();

    XskSetupPreBind(&Socket, Rx, Tx, RxHookId, TxHookId);

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
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);
    HRESULT BindResult;
    do {
        BindResult = XdpApi->XskBind(Socket.Handle.get(), IfIndex, QueueId, BindFlags);
        if (SUCCEEDED(BindResult)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_HRESULT(BindResult);

    TEST_HRESULT(XdpApi->XskActivate(Socket.Handle.get(), XSK_ACTIVATE_FLAG_NONE));

    XskSetupPostBind(&Socket, Rx, Tx);

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
    auto Socket = CreateAndBindSocket(IfIndex, QueueId, Rx, Tx, XdpMode);

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
    UINT64 Descriptor = Socket->FreeDescriptors.top();
    Socket->FreeDescriptors.pop();
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
XSK_BUFFER_DESCRIPTOR *
SocketGetRxDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    return (XSK_BUFFER_DESCRIPTOR *)XskRingGetElement(&Socket->Rings.Rx, Index);
}

static
XSK_BUFFER_DESCRIPTOR *
SocketGetTxDesc(
    _In_ const MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    return (XSK_BUFFER_DESCRIPTOR *)XskRingGetElement(&Socket->Rings.Tx, Index);
}

static
XSK_BUFFER_DESCRIPTOR *
SocketGetAndFreeRxDesc(
    _In_ MY_SOCKET *Socket,
    _In_ UINT32 Index
    )
{
    XSK_BUFFER_DESCRIPTOR * RxDesc = SocketGetRxDesc(Socket, Index);
    Socket->FreeDescriptors.push(XskDescriptorGetAddress(RxDesc->address));
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
    _In_opt_ std::chrono::milliseconds Timeout = TEST_TIMEOUT_ASYNC
    )
{
    UINT32 Index;
    Stopwatch<std::chrono::milliseconds> watchdog(Timeout);
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
    _In_opt_ std::chrono::milliseconds Timeout = TEST_TIMEOUT_ASYNC
    )
{
    Stopwatch<std::chrono::milliseconds> watchdog(Timeout);
    while (!watchdog.IsExpired()) {
        if (XskRingProducerNeedPoke(Ring) == ExpectedState) {
            break;
        }
    }

    TEST_EQUAL(ExpectedState, XskRingProducerNeedPoke(Ring));
}

static
wil::unique_handle
MpOpenGeneric(
    _In_ UINT32 IfIndex
    )
{
    wil::unique_handle Handle;
    TEST_HRESULT(FnMpOpenGeneric(IfIndex, &Handle));
    return Handle;
}

static
wil::unique_handle
MpOpenNative(
    _In_ UINT32 IfIndex
    )
{
    wil::unique_handle Handle;
    TEST_HRESULT(FnMpOpenNative(IfIndex, &Handle));
    return Handle;
}

static
wil::unique_handle
MpOpenAdapter(
    _In_ UINT32 IfIndex
    )
{
    wil::unique_handle Handle;
    TEST_HRESULT(FnMpOpenAdapter(IfIndex, &Handle));
    return Handle;
}

static
wil::unique_handle
LwfOpenDefault(
    _In_ UINT32 IfIndex
    )
{
    wil::unique_handle Handle;
    HRESULT Result;

    //
    // The LWF may not be bound immediately after an adapter restart completes,
    // so poll for readiness.
    //

    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);
    do {
        Result = FnLwfOpenDefault(IfIndex, &Handle);
        if (SUCCEEDED(Result)) {
            break;
        } else {
            TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), Result);
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_HRESULT(Result);

    return Handle;
}

static
BOOLEAN
LwfIsDatapathActive(
    _In_ const wil::unique_handle& Handle
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
    _In_ const wil::unique_handle& Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    return FnMpRxEnqueue(Handle.get(), &RxFrame->Frame);
}

[[nodiscard]]
static
HRESULT
MpRxFlush(
    _In_ const wil::unique_handle& Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    return FnMpRxFlush(Handle.get(), Options);
}

[[nodiscard]]
static
HRESULT
MpRxIndicateFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    HRESULT Status = MpRxEnqueueFrame(Handle, RxFrame);
    if (!SUCCEEDED(Status)) {
        return Status;
    }
    return MpRxFlush(Handle);
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
    _In_ UCHAR *FrameBuffer,
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

static
VOID
MpTxFilter(
    _In_ const wil::unique_handle& Handle,
    _In_ VOID *Pattern,
    _In_ VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_HRESULT(FnMpTxFilter(Handle.get(), Pattern, Mask, Length));
}

static
HRESULT
MpTxGetFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Index,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    return FnMpTxGetFrame(Handle.get(), Index, FrameBufferLength, Frame);
}

static
unique_malloc_ptr<DATA_FRAME>
MpTxAllocateAndGetFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Index
    )
{
    unique_malloc_ptr<DATA_FRAME> FrameBuffer;
    UINT32 FrameLength = 0;
    HRESULT Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = MpTxGetFrame(Handle, Index, &FrameLength, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
    TEST_TRUE(FrameLength >= sizeof(DATA_FRAME));
    FrameBuffer.reset((DATA_FRAME *)malloc(FrameLength));
    TEST_TRUE(FrameBuffer != NULL);

    TEST_HRESULT(MpTxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get()));

    return FrameBuffer;
}

static
VOID
MpTxDequeueFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Index
    )
{
    HRESULT Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

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
    _In_ const wil::unique_handle& Handle
    )
{
    TEST_HRESULT(FnMpTxFlush(Handle.get()));
}

static
VOID
LwfTxEnqueue(
    _In_ const wil::unique_handle& Handle,
    _In_ DATA_FRAME *Frame
    )
{
    TEST_HRESULT(FnLwfTxEnqueue(Handle.get(), Frame));
}

static
VOID
LwfTxFlush(
    _In_ const wil::unique_handle& Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    TEST_HRESULT(FnLwfTxFlush(Handle.get(), Options));
}

static
VOID
LwfRxFilter(
    _In_ const wil::unique_handle& Handle,
    _In_ VOID *Pattern,
    _In_ VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_HRESULT(FnLwfRxFilter(Handle.get(), Pattern, Mask, Length));
}

static
HRESULT
LwfRxGetFrame(
    _In_ const wil::unique_handle& Handle,
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
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Index
    )
{
    unique_malloc_ptr<DATA_FRAME> FrameBuffer;
    UINT32 FrameLength = 0;
    HRESULT Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNLWF for RX: the driver doesn't support overlapped IO.
    //
    do {
        Result = LwfRxGetFrame(Handle, Index, &FrameLength, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
    TEST_TRUE(FrameLength >= sizeof(DATA_FRAME));
    FrameBuffer.reset((DATA_FRAME *)malloc(FrameLength));
    TEST_TRUE(FrameBuffer != NULL);

    TEST_HRESULT(LwfRxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get()));

    return FrameBuffer;
}

static
VOID
LwfRxDequeueFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Index
    )
{
    HRESULT Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

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
    _In_ const wil::unique_handle& Handle
    )
{
    TEST_HRESULT(FnLwfRxFlush(Handle.get()));
}

static
HRESULT
LwfOidSubmitRequest(
    _In_ const wil::unique_handle& Handle,
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
    _In_ const wil::unique_handle& Handle,
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

    InformationBuffer.reset((T *)malloc(InformationBufferLength));
    TEST_TRUE(InformationBuffer.get() != NULL);

    Result = LwfOidSubmitRequest(Handle, Key, &InformationBufferLength, InformationBuffer.get());
    TEST_HRESULT(Result);
    TEST_TRUE(InformationBufferLength > 0);
    *BytesReturned = InformationBufferLength;

    return InformationBuffer;
}

static
VOID
LwfStatusSetFilter(
    _In_ const wil::unique_handle& Handle,
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
    _In_ const wil::unique_handle& Handle,
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
    _In_ const wil::unique_handle& Handle,
    _Out_ UINT32 *StatusBufferLength
    )
{
    HRESULT Result;
    unique_malloc_ptr<T> StatusBuffer;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    *StatusBufferLength = 0;

    //
    // Poll FNLWF for status indication: the driver doesn't support overlapped IO.
    //
    do {
        Result = LwfStatusGetIndication(Handle, StatusBufferLength, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    if (SUCCEEDED(Result)) {
        TEST_EQUAL(0, *StatusBufferLength);
    } else {
        TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
        TEST_NOT_EQUAL(0, *StatusBufferLength);

        StatusBuffer.reset((T *)malloc(*StatusBufferLength));
        TEST_TRUE(StatusBuffer != NULL);

        TEST_HRESULT(LwfStatusGetIndication(Handle, StatusBufferLength, StatusBuffer.get()));
    }

    return StatusBuffer;
}

static
LARGE_INTEGER
MpGetLastMiniportPauseTimestamp(
    _In_ const wil::unique_handle& Handle
    )
{
    LARGE_INTEGER Timestamp = {0};
    TEST_HRESULT(FnMpGetLastMiniportPauseTimestamp(Handle.get(), &Timestamp));
    return Timestamp;
}

static
VOID
MpSetMtu(
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Mtu
    )
{
    TEST_HRESULT(FnMpSetMtu(Handle.get(), Mtu));
}

static
VOID
MpOidFilter(
    _In_ const wil::unique_handle& Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    )
{
    TEST_HRESULT(FnMpOidFilter(Handle.get(), Keys, KeyCount));
}

static
HRESULT
MpOidGetRequest(
    _In_ const wil::unique_handle& Handle,
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
    _In_ const wil::unique_handle& Handle,
    _In_ OID_KEY Key,
    _Out_ UINT32 *InformationBufferLength
    )
{
    unique_malloc_ptr<VOID> InformationBuffer;
    UINT32 Length = 0;
    HRESULT Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNMP for an OID: the driver doesn't support overlapped IO.
    //
    do {
        Result = MpOidGetRequest(Handle, Key, &Length, NULL);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
    TEST_TRUE(Length > 0);
    InformationBuffer.reset(malloc(Length));
    TEST_TRUE(InformationBuffer != NULL);

    TEST_HRESULT(MpOidGetRequest(Handle, Key, &Length, InformationBuffer.get()));

    *InformationBufferLength = Length;
    return InformationBuffer;
}

static
VOID
WaitForWfpQuarantine(
    _In_ const TestInterface& If
    );

static
wil::unique_socket
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

    wil::unique_socket Socket(socket(Af, SOCK_DGRAM, IPPROTO_UDP));
    TEST_NOT_NULL(Socket.get());

    SOCKADDR_INET Address = {0};
    Address.si_family = Af;
    TEST_EQUAL(0, bind(Socket.get(), (SOCKADDR *)&Address, sizeof(Address)));

    INT AddressLength = sizeof(Address);
    TEST_EQUAL(0, getsockname(Socket.get(), (SOCKADDR *)&Address, &AddressLength));

    INT TimeoutMs = (INT)std::chrono::milliseconds(TEST_TIMEOUT_ASYNC).count();
    TEST_EQUAL(
        0,
        setsockopt(Socket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));

    *LocalPort = SS_PORT(&Address);
    return Socket;
}

static
VOID
WaitForWfpQuarantine(
    _In_ const TestInterface& If
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
    Stopwatch<std::chrono::seconds> Watchdog(std::chrono::seconds(30));
    DWORD Bytes;
    do {
        RX_FRAME RxFrame;
        RxInitializeFrame(&RxFrame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        if (SUCCEEDED(MpRxIndicateFrame(GenericMp, &RxFrame))) {
            Bytes = recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0);
        } else {
            Bytes = (DWORD)-1;
        }

        if (Bytes == sizeof(UdpPayload)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_EQUAL(Bytes, sizeof(UdpPayload));
}

static
VOID
WaitForNdisDatapath(
    _In_ const TestInterface& If
    )
{
    CHAR CmdBuff[256];
    BOOLEAN AdapterUp = FALSE;
    BOOLEAN LwfUp = FALSE;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

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
            "%s /c exit (Get-NetAdapter -InterfaceDescription \"%s\").Status -eq \"Up\"",
            PowershellPrefix, If.GetIfDesc());
        AdapterUp = !!system(CmdBuff);

        wil::unique_handle FnLwf = LwfOpenDefault(If.GetIfIndex());
        LwfUp = LwfIsDatapathActive(FnLwf);
    } while (Sleep(POLL_INTERVAL_MS), !(AdapterUp && LwfUp) && !Watchdog.IsExpired());

    TEST_TRUE(AdapterUp);
    TEST_TRUE(LwfUp);
}

static
VOID
ClearMaskedBits(
    _Inout_ XDP_INET_ADDR *Ip,
    _In_ CONST XDP_INET_ADDR *Mask,
    _In_ ADDRESS_FAMILY Af
    )
{
    if (Af == AF_INET) {
        Ip->Ipv4.s_addr &= Mask->Ipv4.s_addr;
    } else {
        UINT64 *Ip64 = (UINT64 *)Ip;
        CONST UINT64 *Mask64 = (CONST UINT64 *)Mask;

        Ip64[0] &= Mask64[0];
        Ip64[1] &= Mask64[1];
    }
}

bool
TestSetup()
{
    WSADATA WsaData;
    WPP_INIT_TRACING(NULL);
    XdpApi = OpenApi();
    PowershellPrefix = GetPowershellPrefix();
    TEST_EQUAL(0, WSAStartup(MAKEWORD(2,2), &WsaData));
    TEST_EQUAL(0, system("netsh advfirewall firewall add rule name=xdpfntest dir=in action=allow protocol=any remoteip=any localip=any"));
    WaitForWfpQuarantine(FnMpIf);
    WaitForNdisDatapath(FnMpIf);
    WaitForWfpQuarantine(FnMp1QIf);
    WaitForNdisDatapath(FnMp1QIf);
    return true;
}

bool
TestCleanup()
{
    TEST_EQUAL(0, system("netsh advfirewall firewall delete rule name=xdpfntest"));
    TEST_EQUAL(0, WSACleanup());
    XdpApi.reset();
    WPP_CLEANUP();
    return true;
}

VOID
MpXdpRegister(
    _In_ const wil::unique_handle& Handle
    )
{
    TEST_HRESULT(FnMpXdpRegister(Handle.get()));
}

static
VOID
MpXdpDeregister(
    _In_ const wil::unique_handle& Handle
    )
{
    TEST_HRESULT(FnMpXdpDeregister(Handle.get()));
}

//
// Tests
//

VOID
OpenApiTest()
{
    unique_xdp_api XdpApiTable = OpenApi();
    XdpCloseApi(XdpApiTable.get());
    XdpApiTable.release();

    TEST_FALSE(SUCCEEDED(TryOpenApi(XdpApiTable, XDP_VERSION_PRERELEASE + 1)));
}

VOID
LoadApiTest()
{
    XDP_LOAD_API_CONTEXT XdpLoadApiContext;
    const XDP_API_TABLE *XdpApiTable;

    TEST_HRESULT(XdpLoadApi(XDP_VERSION_PRERELEASE, &XdpLoadApiContext, &XdpApiTable));
    XdpUnloadApi(XdpLoadApiContext, XdpApiTable);

    TEST_FALSE(SUCCEEDED(XdpLoadApi(XDP_VERSION_PRERELEASE + 1, &XdpLoadApiContext, &XdpApiTable)));
}

static
VOID
BindingTest(
    _In_ const TestInterface& If,
    _In_ BOOLEAN RestartAdapter
    )
{
    for (auto Case : RxTxTestCases) {
        //
        // Create and close an XSK.
        //
        {
            auto Socket =
                CreateAndBindSocket(
                    If.GetIfIndex(), If.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);

            if (RestartAdapter) {
                Stopwatch<std::chrono::milliseconds> Timer(MP_RESTART_TIMEOUT);
                If.Restart(FALSE);
                TEST_FALSE(Timer.IsExpired());
            }
        }

        //
        // Create an XSK and RX program, then detach the program, then the XSK.
        //
        if (Case.Rx) {
            auto Socket =
                SetupSocket(If.GetIfIndex(), If.GetQueueId(), Case.Rx, Case.Tx, XDP_GENERIC);

            if (RestartAdapter) {
                Stopwatch<std::chrono::milliseconds> Timer(MP_RESTART_TIMEOUT);
                If.Restart(FALSE);
                TEST_FALSE(Timer.IsExpired());
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
        WaitForNdisDatapath(If);
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
    CONST UCHAR BufferVa[] = "GenericRxSingleFrame";

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
    TEST_HRESULT(MpRxFlush(GenericMp));

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
    TEST_EQUAL(Buffer.DataLength, RxDesc->length);
    TEST_TRUE(
        RtlEqualMemory(
            Socket.Umem.Buffer.get() +
                XskDescriptorGetAddress(RxDesc->address) + XskDescriptorGetOffset(RxDesc->address),
            Buffer.VirtualAddress + Buffer.DataOffset,
            Buffer.DataLength));
}

VOID
GenericRxBackfillAndTrailer()
{
    auto Socket = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(FnMpIf.GetIfIndex());

    DATA_BUFFER Buffer = {0};
    CONST UCHAR BufferVa[] = "GenericRxBackfillAndTrailer";

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
    TEST_HRESULT(MpRxFlush(GenericMp));

    UINT32 ConsumerIndex = SocketConsumerReserve(&Socket.Rings.Rx, 1);

    //
    // Verify the NBL propagated correctly to XSK.
    //
    TEST_EQUAL(1, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
    auto RxDesc = SocketGetAndFreeRxDesc(&Socket, ConsumerIndex);
    TEST_EQUAL(Buffer.DataLength, RxDesc->length);
    TEST_TRUE(
        RtlEqualMemory(
            Socket.Umem.Buffer.get() +
                XskDescriptorGetAddress(RxDesc->address) + XskDescriptorGetOffset(RxDesc->address),
            Buffer.VirtualAddress + Buffer.DataOffset,
            Buffer.DataLength));
}

VOID
GenericRxMatchUdp(
    _In_ ADDRESS_FAMILY Af,
    _In_ XDP_MATCH_TYPE MatchType
    )
{
    auto If = FnMpIf;
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;

    auto UdpSocket = CreateUdpSocket(Af, &If, &LocalPort);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    wil::unique_handle ProgramHandle;
    unique_malloc_ptr<UINT8> PortSet;

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

    const UCHAR GenericUdpPayload[] = "GenericRxMatchUdp";
    const UCHAR QuicLongHdrUdpPayload[40] = {
        0x80, // IsLongHeader
        0x01, 0x00, 0x00, 0x00, // Version
        0x08, // DestCidLength
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // DestCid
        0x08, // SrcCidLength
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // SrcCid
        0x00  // The rest
    };
    const UCHAR QuicShortHdrUdpPayload[20] = {
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

    CHAR RecvPayload[sizeof(QuicLongHdrUdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(QuicLongHdrUdpPayload)];
    const UCHAR* UdpPayload;
    UINT16 UdpPayloadLength;
    if (MatchType == XDP_MATCH_QUIC_FLOW_SRC_CID) {
        UdpPayload = QuicLongHdrUdpPayload;
        UdpPayloadLength = sizeof(QuicLongHdrUdpPayload);
    } else if (MatchType == XDP_MATCH_QUIC_FLOW_DST_CID) {
        UdpPayload = QuicShortHdrUdpPayload;
        UdpPayloadLength = sizeof(QuicShortHdrUdpPayload);
    } else {
        UdpPayload = GenericUdpPayload;
        UdpPayloadLength = sizeof(GenericUdpPayload);
    }

    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, UdpPayloadLength, &LocalHw,
            &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    XDP_RULE Rule = {};
    Rule.Match = MatchType;
    if (MatchType == XDP_MATCH_UDP_DST) {
        Rule.Pattern.Port = LocalPort;
    } else if (MatchType == XDP_MATCH_IPV4_UDP_TUPLE || MatchType == XDP_MATCH_IPV6_UDP_TUPLE) {
        Rule.Pattern.Tuple.SourcePort = RemotePort;
        Rule.Pattern.Tuple.DestinationPort = LocalPort;
        memcpy(&Rule.Pattern.Tuple.SourceAddress, &RemoteIp, sizeof(INET_ADDR));
        memcpy(&Rule.Pattern.Tuple.DestinationAddress, &LocalIp, sizeof(INET_ADDR));
    } else if (MatchType == XDP_MATCH_QUIC_FLOW_SRC_CID ||
               MatchType == XDP_MATCH_QUIC_FLOW_DST_CID) {
        Rule.Pattern.QuicFlow.UdpPort = LocalPort;
        Rule.Pattern.QuicFlow.CidOffset = 2; // Some arbitrary offset.
        Rule.Pattern.QuicFlow.CidLength = 4; // Some arbitrary length.
        memcpy(
            Rule.Pattern.QuicFlow.CidData,
            CorrectQuicCid + Rule.Pattern.QuicFlow.CidOffset,
            Rule.Pattern.QuicFlow.CidLength);
    } else if (MatchType == XDP_MATCH_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV4_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV6_UDP_PORT_SET) {
        PortSet.reset((UINT8 *)calloc(XDP_PORT_SET_BUFFER_SIZE, 1));
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
    RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

    //
    // Verify XDP drop action.
    //
    ProgramHandle.reset();
    Rule.Action = XDP_PROGRAM_ACTION_DROP;

    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_EQUAL(SOCKET_ERROR, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_EQUAL(WSAETIMEDOUT, WSAGetLastError());

    //
    // Redirect action is implicitly covered by XSK tests.
    //

    if (Rule.Match == XDP_MATCH_UDP_DST) {
        //
        // Verify default action (when no rules match) is pass. Test only makes sense when
        // specific port matching is enabled.
        //
        ProgramHandle.reset();
        Rule.Pattern.Port = htons(ntohs(LocalPort) - 1);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

    } else if (Rule.Match == XDP_MATCH_IPV4_UDP_TUPLE || Rule.Match == XDP_MATCH_IPV6_UDP_TUPLE) {
        //
        // Verify source port matching.
        //
        ProgramHandle.reset();
        Rule.Pattern.Tuple.SourcePort = htons(ntohs(RemotePort) - 1);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        //
        // Verify destination port matching.
        //
        ProgramHandle.reset();
        Rule.Pattern.Tuple.SourcePort = RemotePort; // Revert previous test change
        Rule.Pattern.Tuple.DestinationPort = htons(ntohs(LocalPort) - 1);

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        //
        // Verify source address matching.
        //
        ProgramHandle.reset();
        Rule.Pattern.Tuple.DestinationPort = LocalPort; // Revert previous test change
        (*((UCHAR*)&Rule.Pattern.Tuple.SourceAddress))++;

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        //
        // Verify destination address matching.
        //
        ProgramHandle.reset();
        (*((UCHAR*)&Rule.Pattern.Tuple.SourceAddress))--; // Revert previous test change
        (*((UCHAR*)&Rule.Pattern.Tuple.DestinationAddress))++;

        ProgramHandle =
            CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

    } else if (Rule.Match == XDP_MATCH_QUIC_FLOW_SRC_CID ||
               Rule.Match == XDP_MATCH_QUIC_FLOW_DST_CID) {
        //
        // Verify other header QUIC packets don't match.
        //
        if (Rule.Match == XDP_MATCH_QUIC_FLOW_SRC_CID) {
            UdpPayload = QuicShortHdrUdpPayload;
            UdpPayloadLength = sizeof(QuicShortHdrUdpPayload);
        } else {
            UdpPayload = QuicLongHdrUdpPayload;
            UdpPayloadLength = sizeof(QuicLongHdrUdpPayload);
        }
        UdpFrameLength = sizeof(UdpFrame);
        TEST_TRUE(
            PktBuildUdpFrame(
                UdpFrame, &UdpFrameLength, UdpPayload, UdpPayloadLength, &LocalHw,
                &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        //
        // Revert UDP payload change from test above.
        //
        if (Rule.Match == XDP_MATCH_QUIC_FLOW_SRC_CID) {
            UdpPayload = QuicLongHdrUdpPayload;
            UdpPayloadLength = sizeof(QuicLongHdrUdpPayload);
        } else {
            UdpPayload = QuicShortHdrUdpPayload;
            UdpPayloadLength = sizeof(QuicShortHdrUdpPayload);
        }
        UdpFrameLength = sizeof(UdpFrame);
        TEST_TRUE(
            PktBuildUdpFrame(
                UdpFrame, &UdpFrameLength, UdpPayload, UdpPayloadLength, &LocalHw,
                &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort, RemotePort));

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

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

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

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

    } else if (MatchType == XDP_MATCH_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV4_UDP_PORT_SET ||
               MatchType == XDP_MATCH_IPV6_UDP_PORT_SET) {

        //
        // Verify destination port matching.
        //
        TEST_EQUAL(XDP_PROGRAM_ACTION_DROP, Rule.Action);
        ClearBit(PortSet.get(), LocalPort);

        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        SetBit(PortSet.get(), LocalPort);
        RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
        TEST_EQUAL(SOCKET_ERROR, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_EQUAL(WSAETIMEDOUT, WSAGetLastError());

        if (MatchType == XDP_MATCH_IPV4_UDP_PORT_SET || MatchType == XDP_MATCH_IPV6_UDP_PORT_SET) {
            //
            // Verify destination address matching.
            //
            ProgramHandle.reset();
            (*((UCHAR*)&Rule.Pattern.IpPortSet.Address))++;

            ProgramHandle =
                CreateXdpProg(
                    If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

            RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
            TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
            TEST_EQUAL(
                UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
            TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));
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
    TEST_EQUAL(SOCKET_ERROR, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_EQUAL(WSAETIMEDOUT, WSAGetLastError());

    //
    // Verify IP prefix mismatch.
    //
    ProgramHandle.reset();
    *(UCHAR *)&Rule.Pattern.IpMask.Address ^= 0xFFu;

    ProgramHandle =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    RxInitializeFrame(&Frame, If.GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_HRESULT(MpRxIndicateFrame(GenericMp, &Frame));
    TEST_EQUAL(sizeof(UdpPayload), recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
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
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);

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
    CONST UINT32 NumMatchFrames = 4;
    CONST UINT32 NumNoMatchFrames = 4;

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
    TEST_HRESULT(MpRxFlush(GenericMp, &FlushOptions));

    //
    // Verify the match NBLs propagated correctly to XSK.
    //
    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, NumMatchFrames);
    TEST_EQUAL(NumMatchFrames, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));

    for (UINT32 Index = 0; Index < NumMatchFrames; Index++) {
        auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex++);
        TEST_EQUAL(UdpMatchFrameLength, RxDesc->length);
        TEST_TRUE(
            RtlEqualMemory(
                Xsk.Umem.Buffer.get() + XskDescriptorGetAddress(RxDesc->address) +
                    XskDescriptorGetOffset(RxDesc->address),
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
            recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpNoMatchPayload, RecvPayload, sizeof(UdpNoMatchPayload)));
    }

    TEST_EQUAL(SOCKET_ERROR, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_EQUAL(WSAETIMEDOUT, WSAGetLastError());
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
            CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
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
        TEST_EQUAL(Buffer.DataLength, RxDesc->length);
        TEST_TRUE(
            RtlEqualMemory(
                Socket.Umem.Buffer.get() +
                    XskDescriptorGetAddress(RxDesc->address) + XskDescriptorGetOffset(RxDesc->address),
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
            CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);
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
                &Rule, 1, XDP_CREATE_PROGRAM_FLAG_SHARE);
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
            Sleep(TEST_TIMEOUT_ASYNC_MS);
            TEST_EQUAL(0, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
        } else {
            UINT32 ConsumerIndex = SocketConsumerReserve(&Socket.Rings.Rx, 1);

            TEST_EQUAL(1, XskRingConsumerReserve(&Socket.Rings.Rx, MAXUINT32, &ConsumerIndex));
            auto RxDesc = SocketGetAndFreeRxDesc(&Socket, ConsumerIndex);
            TEST_EQUAL(Buffer.DataLength, RxDesc->length);
            TEST_TRUE(
                RtlEqualMemory(
                    Socket.Umem.Buffer.get() +
                        XskDescriptorGetAddress(RxDesc->address) + XskDescriptorGetOffset(RxDesc->address),
                    Buffer.VirtualAddress + Buffer.DataOffset,
                    Buffer.DataLength));
        }
    }
}

VOID
GenericRxMultiProgramConflicts()
{
    auto If = FnMpIf;
    XDP_RULE Rule = {};

    Rule.Match = XDP_MATCH_ALL;
    Rule.Action = XDP_PROGRAM_ACTION_PASS;

    //
    // Verify a non-sharing program prevents sharing.
    //
    wil::unique_handle failProgram;
    wil::unique_handle validProgram =
        CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    //
    // A non-sharing program should fail with ERROR_OBJECT_ALREADY_EXISTS.
    //
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_OBJECT_ALREADY_EXISTS),
        TryCreateXdpProg(
            failProgram, If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1));

    //
    // A sharing program should fail with ERROR_SHARING_VIOLATION.
    //
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION),
        TryCreateXdpProg(
            failProgram, If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1,
            XDP_CREATE_PROGRAM_FLAG_SHARE));

    //
    // Verify a sharing program prevents sharing with non-sharing programs.
    //
    validProgram.reset();
    validProgram =
        CreateXdpProg(
            If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1,
            XDP_CREATE_PROGRAM_FLAG_SHARE);

    //
    // A non-sharing program should fail with ERROR_OBJECT_ALREADY_EXISTS.
    //
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_OBJECT_ALREADY_EXISTS),
        TryCreateXdpProg(
            failProgram, If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1));
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

    const UCHAR QuicShortHdrUdpPayload[QUIC_MAX_CID_LENGTH + 10] = { // 21 bytes is a full CID
        0x00, // IsLongHeader
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // DestCid
        0x00 // The rest
    };
    const UCHAR CorrectQuicCid[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    std::vector<DATA_BUFFER> Buffers;
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
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        //
        // Test Payload Fragmented at all points
        //

        for (UINT16 FragmentOffset = 0; FragmentOffset < UdpPayloadLength; FragmentOffset++) {
            Buffers.clear();
            TotalOffset = 0;

            std::memset(&Buffer, 0, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = UdpFrameLength - (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            Buffers.push_back(Buffer);

            std::memset(&Buffer, 0, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            Buffers.push_back(Buffer);

            RxInitializeFrame(&RxFrame, If.GetQueueId(), Buffers.data(), (UINT16)Buffers.size());
            TEST_HRESULT(MpRxIndicateFrame(GenericMp, &RxFrame));
            TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
            TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));
        }
    }
}

VOID
GenericRxUdpFragmentQuicLongHeader(
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

    std::vector<DATA_BUFFER> Buffers;
    DATA_BUFFER Buffer = {0};

    XDP_RULE Rules[2];
    Rules[0].Action = XDP_PROGRAM_ACTION_DROP;
    Rules[0].Match = XDP_MATCH_QUIC_FLOW_SRC_CID;
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
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(QuicLongHdrUdpPayload)];
    const UCHAR* UdpPayload;
    UINT16 UdpPayloadLength = 0;
    UdpPayload = QuicLongHdrUdpPayload;

    for (; UdpPayloadLength < sizeof(QuicLongHdrUdpPayload); UdpPayloadLength++) {
        if (UdpPayloadLength == MinQuicHdrLength) {
            Rules[0].Action = XDP_PROGRAM_ACTION_PASS;
            Rules[1].Action = XDP_PROGRAM_ACTION_DROP;
            ProgramHandle.reset();
            ProgramHandle =
                CreateXdpProg(If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, Rules, 2);
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
        TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
        TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));

        //
        // Test Payload Fragmented at all points
        //

        for (UINT16 FragmentOffset = 0; FragmentOffset < UdpPayloadLength; FragmentOffset++) {

            Buffers.clear();
            TotalOffset = 0;

            std::memset(&Buffer, 0, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = UdpFrameLength - (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            Buffers.push_back(Buffer);

            std::memset(&Buffer, 0, sizeof(Buffer));
            Buffer.DataOffset = 0;
            Buffer.DataLength = (FragmentOffset + 1);
            Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
            Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;
            TotalOffset += Buffer.BufferLength;
            Buffers.push_back(Buffer);

            RxInitializeFrame(&RxFrame, If.GetQueueId(), Buffers.data(), (UINT16)Buffers.size());
            TEST_HRESULT(MpRxIndicateFrame(GenericMp, &RxFrame));
            TEST_EQUAL(UdpPayloadLength, recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
            TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, UdpPayloadLength));
        }
    }
}

typedef struct _GENERIC_RX_UDP_FRAGMENT_PARAMS {
    _In_ UINT16 UdpPayloadLength;
    _In_ UINT16 Backfill;
    _In_ UINT16 Trailer;
    _In_ UINT16 *SplitIndexes;
    _In_ UINT16 SplitCount;
} GENERIC_RX_UDP_FRAGMENT_PARAMS;

static
VOID
GenericRxUdpFragmentBuffer(
    _In_ ADDRESS_FAMILY Af,
    _In_ CONST GENERIC_RX_UDP_FRAGMENT_PARAMS *Params
    )
{
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    UINT32 UdpFrameOffset = 0;
    UINT32 TotalOffset = 0;

    auto If = FnMpIf;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_GENERIC);

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

    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_UDP_DST;
    Rule.Pattern.Port = LocalPort;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();

    wil::unique_handle ProgramHandle =
        CreateXdpProg(
            If.GetIfIndex(), &XdpInspectRxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

    //
    // Allocate UDP payload and initialize to a pattern.
    //
    std::vector<UCHAR> UdpPayload(Params->UdpPayloadLength);
    std::generate(UdpPayload.begin(), UdpPayload.end(), []{ return (UCHAR)std::rand(); });

    std::vector<UCHAR> UdpFrame(
        Params->Backfill + UDP_HEADER_BACKFILL(Af) + Params->UdpPayloadLength + Params->Trailer);
    UINT32 UdpFrameLength = (UINT32)UdpFrame.size() - Params->Backfill - Params->Trailer;
    TEST_TRUE(
        PktBuildUdpFrame(
            &UdpFrame[0] + Params->Backfill, &UdpFrameLength, &UdpPayload[0],
            (UINT16)UdpPayload.size(), &LocalHw, &RemoteHw, Af, &LocalIp, &RemoteIp, LocalPort,
            RemotePort));

    std::vector<DATA_BUFFER> Buffers;

    //
    // Split up the UDP frame into RX fragment buffers.
    //
    for (UINT16 Index = 0; Index < Params->SplitCount; Index++) {
        DATA_BUFFER Buffer = {0};

        Buffer.DataOffset = Index == 0 ? Params->Backfill : 0;
        Buffer.DataLength = Params->SplitIndexes[Index] - UdpFrameOffset;
        Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength;
        Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;

        UdpFrameOffset += Buffer.DataLength;
        TotalOffset += Buffer.BufferLength;

        Buffers.push_back(Buffer);
    }

    //
    // Produce the final RX fragment buffer.
    //
    DATA_BUFFER Buffer = {0};
    Buffer.DataOffset = Buffers.size() == 0 ? Params->Backfill : 0;
    Buffer.DataLength = UdpFrameLength - UdpFrameOffset;
    Buffer.BufferLength = Buffer.DataOffset + Buffer.DataLength + Params->Trailer;
    Buffer.VirtualAddress = &UdpFrame[0] + TotalOffset;
    Buffers.push_back(Buffer);

    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    RX_FRAME Frame;
    RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), Buffers.data(), (UINT16)Buffers.size());
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));

    //
    // Produce one XSK fill descriptor.
    //
    SocketProduceRxFill(&Xsk, 1);
    TEST_HRESULT(MpRxFlush(GenericMp));

    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, 1);

    //
    // Verify the NBL propagated correctly to XSK.
    //
    TEST_EQUAL(1, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));
    auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex);

    TEST_EQUAL(UdpFrameLength, RxDesc->length);
    TEST_TRUE(
        RtlEqualMemory(
            Xsk.Umem.Buffer.get() +
                XskDescriptorGetAddress(RxDesc->address) + XskDescriptorGetOffset(RxDesc->address),
            &UdpFrame[0] + Params->Backfill,
            UdpFrameLength));
}

VOID
GenericRxUdpFragmentHeaderData(
    _In_ ADDRESS_FAMILY Af
    )
{

    UINT16 SplitIndexes[] = { UDP_HEADER_BACKFILL(Af) };
    GENERIC_RX_UDP_FRAGMENT_PARAMS Params = {0};
    Params.UdpPayloadLength = 23;
    Params.Backfill = 13;
    Params.Trailer = 17;
    Params.SplitIndexes = SplitIndexes;
    Params.SplitCount = RTL_NUMBER_OF(SplitIndexes);
    GenericRxUdpFragmentBuffer(Af, &Params);
}

VOID
GenericRxUdpTooManyFragments(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_UDP_FRAGMENT_PARAMS Params = {0};
    Params.UdpPayloadLength = 512;
    Params.Backfill = 13;
    Params.Trailer = 17;
    std::vector<UINT16> SplitIndexes;
    for (UINT16 Index = 0; Index < Params.UdpPayloadLength - 1; Index++) {
        SplitIndexes.push_back(Index + 1);
    }
    Params.SplitIndexes = &SplitIndexes[0];
    Params.SplitCount = (UINT16)SplitIndexes.size();
    GenericRxUdpFragmentBuffer(Af, &Params);
}

VOID
GenericRxUdpHeaderFragments(
    _In_ ADDRESS_FAMILY Af
    )
{
    GENERIC_RX_UDP_FRAGMENT_PARAMS Params = {0};
    Params.UdpPayloadLength = 43;
    Params.Backfill = 13;
    Params.Trailer = 17;
    UINT16 SplitIndexes[4] = { 0 };
    SplitIndexes[0] = sizeof(ETHERNET_HEADER) / 2;
    SplitIndexes[1] = SplitIndexes[0] + sizeof(ETHERNET_HEADER);
    SplitIndexes[2] = SplitIndexes[1] + 1;
    SplitIndexes[3] = SplitIndexes[2] + ((Af == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER));
    Params.SplitIndexes = SplitIndexes;
    Params.SplitCount = RTL_NUMBER_OF(SplitIndexes);
    GenericRxUdpFragmentBuffer(Af, &Params);
}

VOID
GenericRxFromTxInspect(
    _In_ ADDRESS_FAMILY Af
    )
{
    auto If = FnMp1QIf;
    UINT16 XskPort;
    SOCKADDR_INET DestAddr = {};
    XDP_HOOK_ID RxInspectFromTxL2 = {};

    RxInspectFromTxL2.Layer = XDP_HOOK_L2;
    RxInspectFromTxL2.Direction = XDP_HOOK_TX;
    RxInspectFromTxL2.SubLayer = XDP_HOOK_INSPECT;

    auto UdpSocket = CreateUdpSocket(Af, &If, &XskPort);
    auto Xsk =
        CreateAndBindSocket(
            If.GetIfIndex(), If.GetQueueId(), TRUE, FALSE, XDP_UNSPEC, XSK_BIND_FLAG_NONE,
            &RxInspectFromTxL2);

    XskPort = htons(1234);

    XDP_RULE Rule;
    Rule.Match = XDP_MATCH_UDP_DST;
    Rule.Pattern.Port = XskPort;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Xsk.Handle.get();

    wil::unique_handle ProgramHandle =
        CreateXdpProg(
            If.GetIfIndex(), &RxInspectFromTxL2, If.GetQueueId(), XDP_GENERIC, &Rule, 1);

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
    CHAR UdpPayload[] = "GenericRxFromTxInspectPkt1GenericRxFromTxInspectPkt2";

    if (WSAGetUdpSendMessageSize(UdpSocket.get(), (DWORD *)&UdpSegmentSize) == SOCKET_ERROR) {
        TEST_EQUAL(WSAEINVAL, WSAGetLastError());
        NumFrames = 1;
        UdpSegmentSize = sizeof(UdpPayload) - 1;
    } else {
        NumFrames = 2;
        UdpSegmentSize = (UINT32)(strchr(UdpPayload, '1') - UdpPayload + 1);
        TEST_EQUAL(NO_ERROR, WSASetUdpSendMessageSize(UdpSocket.get(), UdpSegmentSize));
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
    Stopwatch<std::chrono::seconds> Watchdog(std::chrono::seconds(5));
    INT Bytes;
    do {
        Bytes =
            sendto(
                UdpSocket.get(), UdpPayload, NumFrames * UdpSegmentSize, 0,
                (SOCKADDR *)&DestAddr, sizeof(DestAddr));

        if (Bytes != SOCKET_ERROR) {
            break;
        }
        //
        // TCPIP returns WSAENOBUFS when it cannot reference the data path.
        //
        TEST_EQUAL(WSAENOBUFS, WSAGetLastError());
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(NumFrames * UdpSegmentSize, (UINT32)Bytes);

    //
    // Verify the NBL propagated correctly to XSK.
    //
    UINT32 ConsumerIndex = SocketConsumerReserve(&Xsk.Rings.Rx, NumFrames);
    TEST_EQUAL(NumFrames, XskRingConsumerReserve(&Xsk.Rings.Rx, MAXUINT32, &ConsumerIndex));

    for (UINT32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++) {
        auto RxDesc = SocketGetAndFreeRxDesc(&Xsk, ConsumerIndex++);

        TEST_EQUAL(UDP_HEADER_BACKFILL(Af) + UdpSegmentSize, RxDesc->length);
        TEST_TRUE(
            RtlEqualMemory(
                Xsk.Umem.Buffer.get() + UDP_HEADER_BACKFILL(Af) +
                    XskDescriptorGetAddress(RxDesc->address) +
                        XskDescriptorGetOffset(RxDesc->address),
                &UdpPayload[FrameIndex * UdpSegmentSize],
                UdpSegmentSize));
    }
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
        CreateAndBindSocket(
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
    UINT32 UdpFrameLength = Xsk.Umem.Reg.chunkSize - FrameOffset;
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->address = TxBuffer;
    XskDescriptorSetOffset(&TxDesc->address, FrameOffset);
    TxDesc->length = UdpFrameLength;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    TEST_EQUAL(sizeof(UdpPayload), recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
}

VOID
GenericTxSingleFrame()
{
    auto If = FnMpIf;
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    UINT64 Pattern = 0xA5CC7729CE99C16Aui64;
    UINT64 Mask = ~0ui64;

    MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    UINT16 FrameOffset = 13;
    UCHAR Payload[] = "GenericTxSingleFrame";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer + FrameOffset;
    UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
    ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.chunkSize);

    RtlCopyMemory(TxFrame, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame + sizeof(Pattern), Payload, sizeof(Payload));

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->address = TxBuffer;
    XskDescriptorSetOffset(&TxDesc->address, FrameOffset);
    TxDesc->length = TxFrameLength;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
    TEST_EQUAL(1, MpTxFrame->BufferCount);

    CONST DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[0];
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
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    UINT64 Pattern = 0x2865A18EE4DB02F0ui64;
    UINT64 Mask = ~0ui64;

    MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    UINT16 FrameOffset = 13;
    UCHAR Payload[] = "GenericTxOutOfOrder";
    UINT64 TxBuffer0 = SocketFreePop(&Xsk);
    UINT64 TxBuffer1 = SocketFreePop(&Xsk);
    UCHAR *TxFrame0 = Xsk.Umem.Buffer.get() + TxBuffer0 + FrameOffset;
    UCHAR *TxFrame1 = Xsk.Umem.Buffer.get() + TxBuffer1 + FrameOffset;
    UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
    ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.chunkSize);

    RtlCopyMemory(TxFrame0, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame1, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame0 + sizeof(Pattern), Payload, sizeof(Payload));
    RtlCopyMemory(TxFrame1 + sizeof(Pattern), Payload, sizeof(Payload));

    UINT32 ProducerIndex;
    TEST_EQUAL(2, XskRingProducerReserve(&Xsk.Rings.Tx, 2, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc0 = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc0->address = TxBuffer0;
    XskDescriptorSetOffset(&TxDesc0->address, FrameOffset);
    TxDesc0->length = TxFrameLength;
    XSK_BUFFER_DESCRIPTOR *TxDesc1 = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc1->address = TxBuffer1;
    XskDescriptorSetOffset(&TxDesc1->address, FrameOffset);
    TxDesc1->length = TxFrameLength;

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
            CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    }

    for (UINT32 i = 0; i < RTL_NUMBER_OF(Sockets); i++) {
        auto &Xsk = Sockets[i];
        UINT64 Pattern = 0xA5CC7729CE99C16Aui64 + i;
        UINT64 Mask = ~0ui64;

        MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

        UINT16 FrameOffset = 13;
        UCHAR Payload[] = "GenericTxSharing";
        UINT64 TxBuffer = SocketFreePop(&Xsk);
        UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer + FrameOffset;
        UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
        ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.chunkSize);

        RtlCopyMemory(TxFrame, &Pattern, sizeof(Pattern));
        RtlCopyMemory(TxFrame + sizeof(Pattern), Payload, sizeof(Payload));

        UINT32 ProducerIndex;
        TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

        XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
        TxDesc->address = TxBuffer;
        XskDescriptorSetOffset(&TxDesc->address, FrameOffset);
        TxDesc->length = TxFrameLength;
        XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

        XSK_NOTIFY_RESULT_FLAGS NotifyResult;
        NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
        TEST_EQUAL(0, NotifyResult);

        auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
        TEST_EQUAL(1, MpTxFrame->BufferCount);

        CONST DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[0];
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
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());

    UINT64 Pattern = 0x4FA3DF603CC44911ui64;
    UINT64 Mask = ~0ui64;

    MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    UINT16 FrameOffset = 13;
    UCHAR Payload[] = "GenericTxPoke";
    UINT64 TxBuffer = SocketFreePop(&Xsk);
    UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer + FrameOffset;
    UINT32 TxFrameLength = sizeof(Pattern) + sizeof(Payload);
    ASSERT(FrameOffset + TxFrameLength <= Xsk.Umem.Reg.chunkSize);

    RtlCopyMemory(TxFrame, &Pattern, sizeof(Pattern));
    RtlCopyMemory(TxFrame + sizeof(Pattern), Payload, sizeof(Payload));

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->address = TxBuffer;
    XskDescriptorSetOffset(&TxDesc->address, FrameOffset);
    TxDesc->length = TxFrameLength;

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
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);
    auto GenericMp = MpOpenGeneric(If.GetIfIndex());
    auto MtuScopeGuard = wil::scope_exit([&]
    {
        MpSetMtu(GenericMp, FNMP_DEFAULT_MTU);
        //
        // Wait for the MTU changes to quiesce, which may involve a complete NDIS
        // rebind for filters and protocols.
        //
        Sleep(TEST_TIMEOUT_ASYNC_MS);
        WaitForWfpQuarantine(If);
    });

    const UINT32 TestMtu = 2048;
    C_ASSERT(TestMtu != FNMP_DEFAULT_MTU);
    C_ASSERT(TestMtu < DEFAULT_UMEM_CHUNK_SIZE);

    MpSetMtu(GenericMp, TestMtu);

    //
    // The XSK TX path should be torn down after an MTU change.
    //
    Stopwatch<std::chrono::milliseconds> Watchdog(MP_RESTART_TIMEOUT);
    do {
        if (XskRingError(&Xsk.Rings.Tx)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_TRUE(XskRingError(&Xsk.Rings.Tx));

    //
    // Wait for the MTU changes to quiesce.
    //
    Sleep(TEST_TIMEOUT_ASYNC_MS);
    Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), FALSE, TRUE, XDP_GENERIC);

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
    TxDesc->address = TxBuffer;
    TxDesc->length = TestMtu;

    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);
    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);
    SocketConsumerReserve(&Xsk.Rings.Completion, 1);

    XSK_STATISTICS Stats = {0};
    UINT32 StatsSize = sizeof(Stats);
    GetSockopt(Xsk.Handle.get(), XSK_SOCKOPT_STATISTICS, &Stats, &StatsSize);
    TEST_EQUAL(0, Stats.txInvalidDescriptors);

    //
    // Post a TX larger than the MTU and verify the packet was dropped.
    //

    TxBuffer = SocketFreePop(&Xsk);
    TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
    RtlCopyMemory(TxFrame, Payload, sizeof(Payload));

    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->address = TxBuffer;
    TxDesc->length = TestMtu + 1;

    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    Watchdog.Reset();
    do {
        GetSockopt(Xsk.Handle.get(), XSK_SOCKOPT_STATISTICS, &Stats, &StatsSize);

        if (Stats.txInvalidDescriptors == 1) {
            break;
        }
    } while (!Watchdog.IsExpired());
    TEST_EQUAL(1, Stats.txInvalidDescriptors);
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
    Stopwatch<std::chrono::milliseconds> Timer;

    UCHAR Payload[] = "GenericXskWait";

    auto RxIndicate = [&] {
        DATA_BUFFER Buffer = {0};
        Buffer.DataOffset = 0;
        Buffer.DataLength = sizeof(Payload);
        Buffer.BufferLength = Buffer.DataLength;
        Buffer.VirtualAddress = Payload;

        RX_FRAME Frame;
        RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
        TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
        SocketProduceRxFill(&Xsk, 1);
        TEST_HRESULT(MpRxFlush(GenericMp));
    };

    auto TxIndicate = [&] {
        UINT64 TxBuffer = SocketFreePop(&Xsk);
        UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
        UINT32 TxFrameLength = sizeof(Payload);
        ASSERT(TxFrameLength <= Xsk.Umem.Reg.chunkSize);
        RtlCopyMemory(TxFrame, Payload, sizeof(Payload));

        UINT32 ProducerIndex;
        TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

        XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
        TxDesc->address = TxBuffer;
        TxDesc->length = TxFrameLength;
        XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

        XSK_NOTIFY_RESULT_FLAGS PokeResult;
        NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &PokeResult);
        TEST_EQUAL(0, PokeResult);
    };

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
        RxIndicate();
    }

    if (Tx) {
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
        ExpectedResult |= XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE;
    } else {
        //
        // Produce IO that does not satisfy the wait condition.
        //
        TxIndicate();
    }

    //
    // Verify the wait times out when the requested IO is not available.
    //
    Timer.Reset();
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_TIMEOUT),
        TryNotifySocket(Xsk.Handle.get(), NotifyFlags, WaitTimeoutMs, &NotifyResult));
    Timer.ExpectElapsed(std::chrono::milliseconds(WaitTimeoutMs));

    auto AsyncThread = std::async(
        std::launch::async,
        [&] {
            //
            // On another thread, briefly delay execution to give the main test
            // thread a chance to begin waiting. Then, produce RX and TX.
            //
            Sleep(10);

            if (Rx) {
                RxIndicate();
            }

            if (Tx) {
                TxIndicate();
            }
        }
    );

    //
    // Verify the wait succeeds if any of the conditions is true, and that all
    // conditions are eventually met.
    //
    do {
        Timer.Reset(TEST_TIMEOUT_ASYNC);
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
    Timer.ExpectElapsed(std::chrono::milliseconds(WaitTimeoutMs));
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

    auto RxIndicate = [&] {
        DATA_BUFFER Buffer = {0};
        Buffer.DataOffset = 0;
        Buffer.DataLength = sizeof(Payload);
        Buffer.BufferLength = Buffer.DataLength;
        Buffer.VirtualAddress = Payload;

        RX_FRAME Frame;
        RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
        TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
        SocketProduceRxFill(&Xsk, 1);
        TEST_HRESULT(MpRxFlush(GenericMp));
    };

    auto TxIndicate = [&] {
        UINT64 TxBuffer = SocketFreePop(&Xsk);
        UCHAR *TxFrame = Xsk.Umem.Buffer.get() + TxBuffer;
        UINT32 TxFrameLength = sizeof(Payload);
        ASSERT(TxFrameLength <= Xsk.Umem.Reg.chunkSize);
        RtlCopyMemory(TxFrame, Payload, sizeof(Payload));

        UINT32 ProducerIndex;
        TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

        XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
        TxDesc->address = TxBuffer;
        TxDesc->length = TxFrameLength;
        XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

        XSK_NOTIFY_RESULT_FLAGS PokeResult;
        NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &PokeResult);
        TEST_EQUAL(0, PokeResult);
    };

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
        RxIndicate();
    }

    if (Tx) {
        NotifyFlags |= XSK_NOTIFY_FLAG_WAIT_TX;
        ExpectedResult |= XSK_NOTIFY_RESULT_FLAG_TX_COMP_AVAILABLE;
    } else {
        //
        // Produce IO that does not satisfy the wait condition.
        //
        TxIndicate();
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

    auto AsyncThread = std::async(
        std::launch::async,
        [&] {
            //
            // On another thread, briefly delay execution to give the main test
            // thread a chance to begin waiting. Then, produce RX and TX.
            //
            Sleep(10);

            if (Rx) {
                RxIndicate();
            }

            if (Tx) {
                TxIndicate();
            }
        }
    );

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
    CONST CHAR *DelayDetachTimeoutRegName = "GenericDelayDetachTimeoutSec";

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

    DelayTimeoutMs = 5 * (DWORD)std::chrono::milliseconds(MP_RESTART_TIMEOUT).count();
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
        Sleep(TEST_TIMEOUT_ASYNC_MS); // Give time for the reg change notification to occur.
        FnMpIf.Restart();
    });
    Sleep(TEST_TIMEOUT_ASYNC_MS); // Give time for the reg change notification to occur.

    FnMpIf.Restart();
    {
        auto Xsk = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Rx, Tx, XDP_GENERIC);
    }
    Stopwatch<std::chrono::milliseconds> Timer(MP_RESTART_TIMEOUT);
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
    Sleep(TEST_TIMEOUT_ASYNC_MS); // Give time for the reg change notification to occur.

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
    Sleep(DelayTimeoutMs - TEST_TIMEOUT_ASYNC_MS);
    LastMpPauseTime = MpGetLastMiniportPauseTimestamp(GenericMp);
    TEST_TRUE(LastMpPauseTime.QuadPart < SocketClosureTime.QuadPart);
    QueryPerformanceCounter(&LowerBoundTime);
    Sleep(TEST_TIMEOUT_ASYNC_MS * 2);
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
    Sleep(DelayTimeoutMs - TEST_TIMEOUT_ASYNC_MS);
    {
        auto Xsk = SetupSocket(FnMpIf.GetIfIndex(), FnMpIf.GetQueueId(), Rx, Tx, XDP_GENERIC);
    }
    Sleep(TEST_TIMEOUT_ASYNC_MS * 2);
    LastMpPauseTime = MpGetLastMiniportPauseTimestamp(GenericMp);
    TEST_TRUE(LastMpPauseTime.QuadPart < SocketClosureTime.QuadPart);
    QueryPerformanceCounter(&LowerBoundTime);
    Sleep(TEST_TIMEOUT_ASYNC_MS * 2);
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
    auto Xsk = CreateAndBindSocket(If.GetIfIndex(), If.GetQueueId(), TRUE, TRUE, XDP_GENERIC);
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

    wil::unique_socket RawSocket(socket(Af, SOCK_RAW, IPPROTO_IP));
    TEST_NOT_NULL(RawSocket.get());

    TEST_EQUAL(0, bind(RawSocket.get(), (SOCKADDR *)&LocalSockAddr, sizeof(LocalSockAddr)));

    DWORD Opt = RCVALL_ON;
    DWORD BytesReturned;
    TEST_EQUAL(
        0,
        WSAIoctl(
            RawSocket.get(), SIO_RCVALL, &Opt, sizeof(Opt), NULL, 0, &BytesReturned, NULL, NULL));

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
    ASSERT(UdpFrameLength <= Xsk.Umem.Reg.chunkSize);

    RtlCopyMemory(TxFrame, UdpFrame, UdpFrameLength);

    UINT32 ProducerIndex;
    TEST_EQUAL(1, XskRingProducerReserve(&Xsk.Rings.Tx, 1, &ProducerIndex));

    XSK_BUFFER_DESCRIPTOR *TxDesc = SocketGetTxDesc(&Xsk, ProducerIndex++);
    TxDesc->address = TxBuffer;
    TxDesc->length = UdpFrameLength;
    XskRingProducerSubmit(&Xsk.Rings.Tx, 1);

    XSK_NOTIFY_RESULT_FLAGS NotifyResult;
    NotifySocket(Xsk.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
    TEST_EQUAL(0, NotifyResult);

    //
    // Verify that TCPIP received the frame.
    //

    TEST_EQUAL(sizeof(UdpPayload), recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));

    //
    // Verify that the XSK did not receive the frame.
    //

    UINT32 ConsumerIndex;
    TEST_EQUAL(0, XskRingConsumerReserve(&Xsk.Rings.Rx, 1, &ConsumerIndex));
}

VOID
FnMpNativeHandleTest()
{
    auto NativeMp = MpOpenNative(FnMpIf.GetIfIndex());

    //
    // Verify exclusivity.
    //
    wil::unique_handle Handle;
    TEST_TRUE(FAILED(FnMpOpenNative(FnMpIf.GetIfIndex(), &Handle)));

    MpXdpRegister(NativeMp);
    MpXdpDeregister(NativeMp);
}

VOID
FnLwfRx()
{
    auto GenericMp = MpOpenGeneric(FnMpIf.GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    CONST UINT32 DataOffset = 3;
    CONST UCHAR Payload[] = "FnLwfRx";
    UINT64 Pattern = 0x2865A18EE4DB02F0ui64;
    UINT64 Mask = ~0ui64;
    CONST UINT32 BufferVaSize = DataOffset + sizeof(Pattern) + sizeof(Payload);
    UCHAR BufferVa[BufferVaSize];

    DATA_BUFFER Buffer = {0};
    Buffer.DataOffset = DataOffset;
    Buffer.DataLength = sizeof(Pattern) + sizeof(Payload);
    Buffer.BufferLength = BufferVaSize;
    Buffer.VirtualAddress = BufferVa;

    RtlCopyMemory(BufferVa + DataOffset, &Pattern, sizeof(Pattern));
    RtlCopyMemory(BufferVa + DataOffset + sizeof(Pattern), Payload, sizeof(Payload));

    LwfRxFilter(DefaultLwf, &Pattern, &Mask, sizeof(Pattern));

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
    TEST_HRESULT(MpRxEnqueueFrame(GenericMp, &Frame));
    TEST_HRESULT(MpRxFlush(GenericMp));

    auto LwfRxFrame = LwfRxAllocateAndGetFrame(DefaultLwf, 0);
    TEST_EQUAL(LwfRxFrame->BufferCount, Frame.Frame.BufferCount);

    CONST DATA_BUFFER *LwfRxBuffer = &LwfRxFrame->Buffers[0];
    TEST_EQUAL(LwfRxBuffer->BufferLength, Buffer.BufferLength);
    TEST_EQUAL(LwfRxBuffer->DataOffset, Buffer.DataOffset);
    TEST_TRUE(
        RtlEqualMemory(
            Buffer.VirtualAddress + Buffer.DataOffset,
            LwfRxBuffer->VirtualAddress + LwfRxBuffer->DataOffset,
            Buffer.DataLength));

    LwfRxDequeueFrame(DefaultLwf, 0);
    LwfRxFlush(DefaultLwf);
}

VOID
FnLwfTx()
{
    auto GenericMp = MpOpenGeneric(FnMpIf.GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    CONST UINT32 DataOffset = 3;
    CONST UCHAR Payload[] = "FnLwfTx";
    UINT64 Pattern = 0x39E8534AA85B4A98ui64;
    UINT64 Mask = ~0ui64;
    CONST UINT32 BufferVaSize = DataOffset + sizeof(Pattern) + sizeof(Payload);
    UCHAR BufferVa[BufferVaSize];

    DATA_FRAME Frame = {0};
    DATA_BUFFER Buffer = {0};
    Frame.BufferCount = 1;
    Buffer.DataOffset = DataOffset;
    Buffer.DataLength = sizeof(Pattern) + sizeof(Payload);
    Buffer.BufferLength = BufferVaSize;
    Buffer.VirtualAddress = BufferVa;
    Frame.Buffers = &Buffer;

    RtlCopyMemory(BufferVa + DataOffset, &Pattern, sizeof(Pattern));
    RtlCopyMemory(BufferVa + DataOffset + sizeof(Pattern), Payload, sizeof(Payload));

    MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    LwfTxEnqueue(DefaultLwf, &Frame);
    LwfTxFlush(DefaultLwf);

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
    TEST_EQUAL(MpTxFrame->BufferCount, Frame.BufferCount);

    CONST DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[0];
    TEST_EQUAL(MpTxBuffer->BufferLength, Buffer.BufferLength);
    TEST_EQUAL(MpTxBuffer->DataOffset, Buffer.DataOffset);
    TEST_TRUE(
        RtlEqualMemory(
            Buffer.VirtualAddress + Buffer.DataOffset,
            MpTxBuffer->VirtualAddress + MpTxBuffer->DataOffset,
            Buffer.DataLength));

    MpTxDequeueFrame(GenericMp, 0);
    MpTxFlush(GenericMp);
}

VOID
FnLwfOid()
{
    OID_KEY OidKeys[2] = {0};
    UINT32 MpInfoBufferLength;
    unique_malloc_ptr<VOID> MpInfoBuffer;
    UINT32 LwfInfoBufferLength;
    ULONG LwfInfoBuffer;
    ULONG OriginalPacketFilter = 0;
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    //
    // Get the existing packet filter from NDIS so we can tweak it to make sure
    // the set OID makes it to the miniport. N.B. this get OID is handled by
    // NDIS, not the miniport.
    //
    OidKeys[0].Oid = OID_GEN_CURRENT_PACKET_FILTER;
    OidKeys[0].RequestType = NdisRequestQueryInformation;
    LwfInfoBufferLength = sizeof(OriginalPacketFilter);
    TEST_HRESULT(
        LwfOidSubmitRequest(DefaultLwf, OidKeys[0], &LwfInfoBufferLength, &OriginalPacketFilter));

    //
    // Get.
    //
    OidKeys[0].Oid = OID_GEN_RECEIVE_BLOCK_SIZE;
    OidKeys[0].RequestType = NdisRequestQueryInformation;

    //
    // Set.
    //
    OidKeys[1].Oid = OID_GEN_CURRENT_PACKET_FILTER;
    OidKeys[1].RequestType = NdisRequestSetInformation;

    for (UINT32 Index = 0; Index < RTL_NUMBER_OF(OidKeys); Index++) {
        auto AdapterMp = MpOpenAdapter(FnMpIf.GetIfIndex());

        MpOidFilter(AdapterMp, &OidKeys[Index], 1);

        LwfInfoBuffer = OriginalPacketFilter ^ (0x00000001);
        LwfInfoBufferLength = sizeof(LwfInfoBuffer);
        auto AsyncThread = std::async(
            std::launch::async,
            [&] {
                return
                    LwfOidSubmitRequest(
                        DefaultLwf, OidKeys[Index], &LwfInfoBufferLength, &LwfInfoBuffer);
            }
        );

        MpInfoBuffer = MpOidAllocateAndGetRequest(AdapterMp, OidKeys[Index], &MpInfoBufferLength);
        AdapterMp.reset();

        TEST_EQUAL(AsyncThread.wait_for(TEST_TIMEOUT_ASYNC), std::future_status::ready);
        TEST_HRESULT(AsyncThread.get());

        TEST_EQUAL(LwfInfoBufferLength, sizeof(ULONG));
    }
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

    RssConfig.reset((XDP_RSS_CONFIGURATION *)malloc(Size));
    TEST_TRUE(RssConfig.get() != NULL);

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

    IndirectionTableOut.reset((PROCESSOR_NUMBER *)malloc(RssConfig->IndirectionTableSize));

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

    RssConfig.reset((XDP_RSS_CONFIGURATION *)malloc(RssConfigSize));
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

    OID_KEY Key {0};
    Key.Oid = OID_GEN_RECEIVE_SCALE_PARAMETERS;
    Key.RequestType = NdisRequestSetInformation;
    MpOidFilter(AdapterMp, &Key, 1);

    auto AsyncThread = std::async(
        std::launch::async,
        [&] {
            return TryRssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize);
        }
    );

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
    TEST_EQUAL(AsyncThread.wait_for(TEST_TIMEOUT_ASYNC), std::future_status::ready);
    TEST_HRESULT(AsyncThread.get());
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
        TEST_HRESULT(MpRxFlush(GenericMp, &FlushOptions));
    }
}

static
VOID
PrintProcArray(
    _In_ const char* Prefix,
    _In_ const std::vector<UINT32> &ProcArray
    )
{
    std::string Msg;

    Msg += Prefix;
    Msg += "[";
    for (int i = 0; i < ProcArray.size(); i++) {
        Msg += std::to_string(ProcArray[i]);
        if (i != ProcArray.size() - 1) {
            Msg += ",";
        }
    }
    Msg += "]";

    TraceVerbose("%s", Msg.c_str());
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

typedef struct _PROCESSOR_NUMBER_COMP {
    bool operator()(const PROCESSOR_NUMBER &A, const PROCESSOR_NUMBER &B) const {
        return (A.Group == B.Group) ? A.Number < B.Number : A.Group < B.Group;
    }
} PROCESSOR_NUMBER_COMP;

static
std::set<PROCESSOR_NUMBER, PROCESSOR_NUMBER_COMP>
GetRssProcessorSetFromIndirectionTable(
    _In_ const PROCESSOR_NUMBER *IndirectionTable,
    _In_ UINT32 IndirectionTableSize
    )
{
    std::set<PROCESSOR_NUMBER, PROCESSOR_NUMBER_COMP> RssProcessors;

    //
    // Convert indirection table to processor set.
    //
    for (UINT32 Index = 0;
        Index < IndirectionTableSize / sizeof(*IndirectionTable);
        Index++) {
        PROCESSOR_NUMBER Processor = IndirectionTable[Index];
        RssProcessors.insert(Processor);
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
    std::set<PROCESSOR_NUMBER, PROCESSOR_NUMBER_COMP> RssProcessors =
        GetRssProcessorSetFromIndirectionTable(IndirectionTable.get(), IndirectionTableSize);

    //
    // Indicate RX on each miniport RSS queue and capture all packets after XDP
    // passes them up.
    //

    auto DefaultLwf = LwfOpenDefault(If.GetIfIndex());
    UCHAR Pattern = 0x00;
    UCHAR Mask = 0x00;
    LwfRxFilter(DefaultLwf, &Pattern, &Mask, sizeof(Pattern));

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
        TEST_EQUAL(1, RssProcessors.erase(Processor));
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
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);
    HRESULT CurrentRssResult;
    do {
        UINT32 CurrentRssConfigSize = 0;
        CurrentRssResult = TryRssGet(InterfaceHandle.get(), NULL, &CurrentRssConfigSize);
        if (CurrentRssResult == HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), CurrentRssResult);

    RssConfig.reset((XDP_RSS_CONFIGURATION *)malloc(RssConfigSize));

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
        ModifiedRssConfig.reset((XDP_RSS_CONFIGURATION *)malloc(ModifiedRssConfigSize));

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

    RssConfig.reset((XDP_RSS_CONFIGURATION *)malloc(OriginalRssConfigSize));
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
        HRESULT_FROM_WIN32(ERROR_BAD_COMMAND),
        TryRssGet(InterfaceHandle.get(), RssConfig.get(), &Size));

    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_BAD_COMMAND),
        TryRssSet(InterfaceHandle.get(), RssConfig.get(), RssConfigSize));

    InterfaceHandle.reset();

    //
    // Verify original RSS settings are restored.
    //

    //
    // Wait for the interface to be up and RSS to be configured by the upper
    // layer protocol.
    //
    Stopwatch<std::chrono::milliseconds> Watchdog(MP_RESTART_TIMEOUT);
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
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
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
    CONST UINT32 LowerXdpRssHashType = XDP_RSS_HASH_TYPE_TCP_IPV4;
    CONST UINT32 UpperXdpRssHashType = XDP_RSS_HASH_TYPE_TCP_IPV6;
    CONST UINT32 UpperNdisRssHashType = NDIS_HASH_TCP_IPV6;
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

    OidKey.Oid = OID_GEN_RECEIVE_SCALE_PARAMETERS;
    OidKey.RequestType = NdisRequestQueryInformation;
    OriginalNdisRssParams =
        LwfOidAllocateAndSubmitRequest<NDIS_RECEIVE_SCALE_PARAMETERS>(
            DefaultLwf, OidKey, &OriginalNdisRssParamsSize);

    InterfaceHandle = InterfaceOpen(FnMpIf.GetIfIndex());
    RssConfig = GetXdpRss(InterfaceHandle, &RssConfigSize);

    //
    // Set lower edge settings via XDP.
    //
    LowerRssConfig.reset((XDP_RSS_CONFIGURATION *)malloc(RssConfigSize));
    LowerRssConfigSize = RssConfigSize;
    RtlCopyMemory(LowerRssConfig.get(), RssConfig.get(), RssConfigSize);
    LowerRssConfig->Flags = XDP_RSS_FLAG_SET_HASH_TYPE;
    LowerRssConfig->HashType = LowerXdpRssHashType;
    RssSet(InterfaceHandle.get(), LowerRssConfig.get(), LowerRssConfigSize);

    //
    // Set upper edge settings via NDIS.
    //
    OidKey.Oid = OID_GEN_RECEIVE_SCALE_PARAMETERS;
    OidKey.RequestType = NdisRequestSetInformation;
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
    OidKey.Oid = OID_GEN_RECEIVE_SCALE_PARAMETERS;
    OidKey.RequestType = NdisRequestQueryInformation;
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
    _In_ const std::vector<UINT32> &ProcessorIndices,
    _Out_ unique_malloc_ptr<PROCESSOR_NUMBER> &IndirectionTable,
    _Out_ UINT32 *IndirectionTableSize
    )
{
    *IndirectionTableSize = (UINT32)ProcessorIndices.size() * sizeof(*IndirectionTable);

    IndirectionTable.reset((PROCESSOR_NUMBER *)malloc(*IndirectionTableSize));
    TEST_TRUE(IndirectionTable.get() != NULL);

    RtlZeroMemory(IndirectionTable.get(), *IndirectionTableSize);
    for (UINT32 i = 0; i < ProcessorIndices.size(); i++) {
        ProcessorIndexToProcessorNumber(ProcessorIndices[i], &IndirectionTable.get()[i]);
    }
}

VOID
OffloadRssSingleSet(
    _In_ const std::vector<UINT32> &ProcessorIndices
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
    _In_ const std::vector<UINT32> &ProcessorIndices1,
    _In_ const std::vector<UINT32> &ProcessorIndices2
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

    OffloadRssSingleSet({0});
    OffloadRssSingleSet({1});
    OffloadRssSingleSet({0,1});

    OffloadRssSubsequentSet({0}, {1});

    if (GetProcessorCount() >= 4) {
        OffloadRssSingleSet({0,2});
        OffloadRssSingleSet({1,3});
        OffloadRssSingleSet({0,1,2,3});

        OffloadRssSubsequentSet({0,2}, {1,3});
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

    RssCapabilities.reset((XDP_RSS_CAPABILITIES *)malloc(Size));
    TEST_TRUE(RssCapabilities.get() != NULL);

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

static
VOID
InitializeOffloadParams(
    _Out_ NDIS_OFFLOAD_PARAMETERS *Params
    )
{
    RtlZeroMemory(Params, sizeof(*Params));
    Params->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params->Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_1;
    Params->Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_1;
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
        InitializeOffloadParams(&OffloadParams);
        OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED;
        OidKey.Oid = Configs[i].Oid;
        OidKey.RequestType = NdisRequestSetInformation;
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
    OidKey.Oid = OID_TCP_OFFLOAD_CURRENT_CONFIG;
    OidKey.RequestType = NdisRequestQueryInformation;
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
    TEST_EQUAL(0, system(CmdBuff));

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(CmdBuff, "%s /c Set-NetAdapterAdvancedProperty -ifDesc \"%s\" -DisplayName UDPChecksumOffloadIPv4 -DisplayValue 'TX Enabled' -NoRestart", PowershellPrefix, If.GetIfDesc());
    TEST_EQUAL(0, system(CmdBuff));

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
            ProcNumberSize = sizeof(ProcNumber);
            Result =
                TryGetSockopt(
                    Socket.Handle.get(), XSK_SOCKOPT_RX_PROCESSOR_AFFINITY, &ProcNumber,
                    &ProcNumberSize);
            TEST_TRUE(FAILED(Result));
            TEST_FALSE(XskRingAffinityChanged(&Socket.Rings.Rx));
        }

        if (Case.Tx) {
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

            CreateIndirectionTable({ProcIndex}, IndirectionTable, &IndirectionTableSize);
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
                TEST_HRESULT(MpRxFlush(GenericMp, &FlushOptions));

                SocketConsumerReserve(&Socket.Rings.Rx, 1);
                XskRingConsumerRelease(&Socket.Rings.Rx, 1);
                TEST_TRUE(XskRingAffinityChanged(&Socket.Rings.Rx));

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
                TxDesc->address = TxBuffer;
                TxDesc->length = sizeof(BufferVa);
                XskRingProducerSubmit(&Socket.Rings.Tx, 1);
                XSK_NOTIFY_RESULT_FLAGS NotifyResult;
                NotifySocket(Socket.Handle.get(), XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
                TEST_EQUAL(0, NotifyResult);
                SocketConsumerReserve(&Socket.Rings.Completion, 1);
                XskRingConsumerRelease(&Socket.Rings.Completion, 1);

                TEST_TRUE(XskRingAffinityChanged(&Socket.Rings.Tx));

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
