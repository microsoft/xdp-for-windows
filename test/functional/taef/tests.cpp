//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <CppUnitTest.h>
#include <xdpapi.h>
#include <fntrace.h>

#include "xdptest.h"
#include "tests.h"
#include "util.h"
#include "tests.tmh"

//
// Test suite(s).
//

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

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
    Assert::IsTrue(TestSetup());
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    Assert::IsTrue(TestCleanup());
}

TEST_CLASS(xdpfunctionaltests)
{
public:
    TEST_METHOD(OpenApi) {
        ::OpenApiTest();
    }

    TEST_METHOD(LoadApi) {
        ::LoadApiTest();
    }

    TEST_METHOD(GenericBinding) {
        ::GenericBinding();
    }

    TEST_METHOD(GenericBindingResetAdapter) {
        ::GenericBindingResetAdapter();
    }

    TEST_METHOD(GenericRxSingleFrame) {
        ::GenericRxSingleFrame();
    }

    TEST_METHOD(GenericRxNoPoke) {
        ::GenericRxNoPoke();
    }

    TEST_METHOD(GenericRxBackfillAndTrailer) {
        ::GenericRxBackfillAndTrailer();
    }

    TEST_METHOD(GenericRxLowResources) {
        ::GenericRxLowResources();
    }

    TEST_METHOD(GenericRxMultiSocket) {
        ::GenericRxMultiSocket();
    }

    TEST_METHOD(GenericRxMultiProgram) {
        ::GenericRxMultiProgram();
    }

    TEST_METHOD(GenericTxToRxInject) {
        ::GenericTxToRxInject();
    }

    TEST_METHOD(GenericTxSingleFrame) {
        ::GenericTxSingleFrame();
    }

    TEST_METHOD(GenericTxOutOfOrder) {
        ::GenericTxOutOfOrder();
    }

    TEST_METHOD(GenericTxSharing) {
        ::GenericTxSharing();
    }

    TEST_METHOD(GenericTxPoke) {
        ::GenericTxPoke();
    }

    TEST_METHOD(GenericTxMtu) {
        ::GenericTxMtu();
    }

    TEST_METHOD(FnMpNativeHandleTest) {
        ::FnMpNativeHandleTest();
    }

    TEST_METHOD(GenericRxTcpControlV4) {
        GenericRxTcpControl(AF_INET);
    }

    TEST_METHOD(GenericRxTcpControlV6) {
        GenericRxTcpControl(AF_INET6);
    }

    TEST_METHOD(GenericRxAllQueueRedirectV4) {
        GenericRxAllQueueRedirect(AF_INET);
    }

    TEST_METHOD(GenericRxAllQueueRedirectV6) {
        GenericRxAllQueueRedirect(AF_INET6);
    }

    TEST_METHOD(GenericRxMatchUdpV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_UDP, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_UDP, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpPortV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_UDP_DST, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpPortV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_UDP_DST, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpTupleV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_IPV4_UDP_TUPLE, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpTupleV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_UDP_TUPLE, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_QUIC_FLOW_SRC_CID, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_QUIC_FLOW_DST_CID, TRUE);
    }

    TEST_METHOD(GenericRxMatchTcpQuicSrcV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE);
    }

    TEST_METHOD(GenericRxMatchTcpQuicSrcV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_SRC_CID, FALSE);
    }

    TEST_METHOD(GenericRxMatchTcpQuicDstV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE);
    }

    TEST_METHOD(GenericRxMatchTcpQuicDstV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_TCP_QUIC_FLOW_DST_CID, FALSE);
    }

    TEST_METHOD(GenericRxMatchIpPrefixV4) {
        GenericRxMatchIpPrefix(AF_INET);
    }

    TEST_METHOD(GenericRxMatchIpPrefixV6) {
        GenericRxMatchIpPrefix(AF_INET6);
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_UDP_PORT_SET, TRUE);
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_UDP_PORT_SET, TRUE);
    }

    TEST_METHOD(GenericRxMatchIpv4UdpPortSet) {
        GenericRxMatch(AF_INET, XDP_MATCH_IPV4_UDP_PORT_SET, TRUE);
    }

    TEST_METHOD(GenericRxMatchIpv6UdpPortSet) {
        GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_UDP_PORT_SET, TRUE);
    }

    TEST_METHOD(GenericRxMatchIpv4TcpPortSet) {
        GenericRxMatch(AF_INET, XDP_MATCH_IPV4_TCP_PORT_SET, FALSE);
    }

    TEST_METHOD(GenericRxMatchIpv6TcpPortSet) {
        GenericRxMatch(AF_INET6, XDP_MATCH_IPV6_TCP_PORT_SET, FALSE);
    }

    TEST_METHOD(GenericRxMatchTcpPortV4) {
        GenericRxMatch(AF_INET, XDP_MATCH_TCP_DST, FALSE);
    }

    TEST_METHOD(GenericRxMatchTcpPortV6) {
        GenericRxMatch(AF_INET6, XDP_MATCH_TCP_DST, FALSE);
    }

    TEST_METHOD(GenericXskWaitRx) {
        GenericXskWait(TRUE, FALSE);
    }

    TEST_METHOD(GenericXskWaitTx) {
        GenericXskWait(FALSE, TRUE);
    }

    TEST_METHOD(GenericXskWaitRxTx) {
        GenericXskWait(TRUE, TRUE);
    }

    TEST_METHOD(GenericXskWaitAsyncRx) {
        GenericXskWaitAsync(TRUE, FALSE);
    }

    TEST_METHOD(GenericXskWaitAsyncTx) {
        GenericXskWaitAsync(FALSE, TRUE);
    }

    TEST_METHOD(GenericXskWaitAsyncRxTx) {
        GenericXskWaitAsync(TRUE, TRUE);
    }

    TEST_METHOD(GenericLwfDelayDetachRx) {
        GenericLwfDelayDetach(TRUE, FALSE);
    }

    TEST_METHOD(GenericLwfDelayDetachTx) {
        GenericLwfDelayDetach(FALSE, TRUE);
    }

    TEST_METHOD(GenericLwfDelayDetachRxTx) {
        GenericLwfDelayDetach(TRUE, TRUE);
    }

    TEST_METHOD(GenericRxUdpFragmentQuicLongHeaderV4) {
        GenericRxUdpFragmentQuicLongHeader(AF_INET, TRUE);
    }

    TEST_METHOD(GenericRxUdpFragmentQuicLongHeaderV6) {
        GenericRxUdpFragmentQuicLongHeader(AF_INET6, TRUE);
    }

    TEST_METHOD(GenericRxTcpFragmentQuicLongHeaderV4) {
        GenericRxUdpFragmentQuicLongHeader(AF_INET, FALSE);
    }

    TEST_METHOD(GenericRxTcpFragmentQuicLongHeaderV6) {
        GenericRxUdpFragmentQuicLongHeader(AF_INET6, FALSE);
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV4) {
        GenericRxUdpFragmentQuicShortHeader(AF_INET);
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV6) {
        GenericRxUdpFragmentQuicShortHeader(AF_INET6);
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV4) {
        GenericRxFragmentHeaderData(AF_INET, TRUE);
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV6) {
        GenericRxFragmentHeaderData(AF_INET6, TRUE);
    }

    TEST_METHOD(GenericRxTcpFragmentHeaderDataV4) {
        GenericRxFragmentHeaderData(AF_INET, FALSE);
    }

    TEST_METHOD(GenericRxTcpFragmentHeaderDataV6) {
        GenericRxFragmentHeaderData(AF_INET6, FALSE);
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV4) {
        GenericRxTooManyFragments(AF_INET, TRUE);
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV6) {
        GenericRxTooManyFragments(AF_INET6, TRUE);
    }

    TEST_METHOD(GenericRxTcpTooManyFragmentsV4) {
        GenericRxTooManyFragments(AF_INET, FALSE);
    }

    TEST_METHOD(GenericRxTcpTooManyFragmentsV6) {
        GenericRxTooManyFragments(AF_INET6, FALSE);
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV4) {
        GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, TRUE);
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV6) {
        GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, TRUE);
    }

    TEST_METHOD(GenericRxTcpHeaderFragmentsV4) {
        GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, FALSE);
    }

    TEST_METHOD(GenericRxTcpHeaderFragmentsV6) {
        GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, FALSE);
    }

    TEST_METHOD(GenericRxL2Fwd) {
        GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE);
    }

    TEST_METHOD(GenericRxL2FwdLowResources) {
        GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, FALSE, TRUE);
    }

    TEST_METHOD(GenericRxL2FwdTxInspect) {
        GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_L2FWD, TRUE, TRUE);
    }

    TEST_METHOD(GenericRxFromTxInspectV4) {
        GenericRxFromTxInspect(AF_INET);
    }

    TEST_METHOD(GenericRxFromTxInspectV6) {
        GenericRxFromTxInspect(AF_INET6);
    }

    TEST_METHOD(SecurityAdjustDeviceAcl) {
        ::SecurityAdjustDeviceAcl();
    }

    TEST_METHOD(GenericRxEbpfAttach) {
        ::GenericRxEbpfAttach();
    }

    TEST_METHOD(GenericRxEbpfDrop) {
        ::GenericRxEbpfDrop();
    }

    TEST_METHOD(GenericRxEbpfPass) {
        ::GenericRxEbpfPass();
    }

    TEST_METHOD(GenericRxEbpfTx) {
        ::GenericRxEbpfTx();
    }

    TEST_METHOD(GenericRxEbpfPayload) {
        ::GenericRxEbpfPayload();
    }

    TEST_METHOD(GenericRxEbpfFragments) {
        ::GenericRxEbpfFragments();
    }

    TEST_METHOD(GenericRxEbpfUnload) {
        ::GenericRxEbpfUnload();
    }

    TEST_METHOD(GenericLoopbackV4) {
        GenericLoopback(AF_INET);
    }

    TEST_METHOD(GenericLoopbackV6) {
        GenericLoopback(AF_INET6);
    }

    TEST_METHOD(OffloadRssError) {
        ::OffloadRssError();
    }

    TEST_METHOD(OffloadRssReference) {
        ::OffloadRssReference();
    }

    TEST_METHOD(OffloadRssInterfaceRestart) {
        ::OffloadRssInterfaceRestart();
    }

    TEST_METHOD(OffloadRssUnchanged) {
        ::OffloadRssUnchanged();
    }

    TEST_METHOD(OffloadRssUpperSet) {
        ::OffloadRssUpperSet();
    }

    TEST_METHOD(OffloadRssSet) {
        ::OffloadRssSet();
    }

    TEST_METHOD(OffloadRssCapabilities) {
        ::OffloadRssCapabilities();
    }

    TEST_METHOD(OffloadRssReset) {
        ::OffloadRssReset();
    }

    TEST_METHOD(FnLwfRx) {
        ::FnLwfRx();
    }

    TEST_METHOD(FnLwfTx) {
        ::FnLwfTx();
    }

    TEST_METHOD(FnLwfOid) {
        ::FnLwfOid();
    }

    TEST_METHOD(OffloadSetHardwareCapabilities) {
        ::OffloadSetHardwareCapabilities();
    }

    TEST_METHOD(GenericXskQueryAffinity) {
        ::GenericXskQueryAffinity();
    }

    TEST_METHOD(OffloadQeoConnection) {
        ::OffloadQeoConnection();
    }

    TEST_METHOD(OffloadQeoRevertInterfaceRemoval) {
        ::OffloadQeoRevert(RevertReasonInterfaceRemoval);
    }

    TEST_METHOD(OffloadQeoRevertHandleClosure) {
        ::OffloadQeoRevert(RevertReasonHandleClosure);
    }

    TEST_METHOD(OffloadQeoOidFailure) {
        ::OffloadQeoOidFailure();
    }
};
