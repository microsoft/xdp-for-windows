//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <CppUnitTest.h>
#include <xdpapi.h>

#include "xdptest.h"
#include "tests.h"
#include "util.h"

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
    _In_z_ const LPWSTR File,
    _In_z_ const LPWSTR Function,
    INT Line,
    _Printf_format_string_ const LPWSTR Format,
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

    Logger::WriteMessage(Buffer);
}

VOID
LogTestWarning(
    _In_z_ const LPWSTR File,
    _In_z_ const LPWSTR Function,
    INT Line,
    _Printf_format_string_ const LPWSTR Format,
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

    TEST_METHOD(GenericRxMultiProgramConflicts) {
        ::GenericRxMultiProgramConflicts();
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

    TEST_METHOD(GenericRxMatchUdpV4) {
        GenericRxMatchUdp(AF_INET, XDP_MATCH_UDP);
    }

    TEST_METHOD(GenericRxMatchUdpV6) {
        GenericRxMatchUdp(AF_INET6, XDP_MATCH_UDP);
    }

    TEST_METHOD(GenericRxMatchUdpPortV4) {
        GenericRxMatchUdp(AF_INET, XDP_MATCH_UDP_DST);
    }

    TEST_METHOD(GenericRxMatchUdpPortV6) {
        GenericRxMatchUdp(AF_INET6, XDP_MATCH_UDP_DST);
    }

    TEST_METHOD(GenericRxMatchUdpTupleV4) {
        GenericRxMatchUdp(AF_INET, XDP_MATCH_IPV4_UDP_TUPLE);
    }

    TEST_METHOD(GenericRxMatchUdpTupleV6) {
        GenericRxMatchUdp(AF_INET6, XDP_MATCH_IPV6_UDP_TUPLE);
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV4) {
        GenericRxMatchUdp(AF_INET, XDP_MATCH_QUIC_FLOW_SRC_CID);
    }

    TEST_METHOD(GenericRxMatchUdpQuicSrcV6) {
        GenericRxMatchUdp(AF_INET6, XDP_MATCH_QUIC_FLOW_SRC_CID);
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV4) {
        GenericRxMatchUdp(AF_INET, XDP_MATCH_QUIC_FLOW_DST_CID);
    }

    TEST_METHOD(GenericRxMatchUdpQuicDstV6) {
        GenericRxMatchUdp(AF_INET6, XDP_MATCH_QUIC_FLOW_DST_CID);
    }

    TEST_METHOD(GenericRxMatchIpPrefixV4) {
        GenericRxMatchIpPrefix(AF_INET);
    }

    TEST_METHOD(GenericRxMatchIpPrefixV6) {
        GenericRxMatchIpPrefix(AF_INET6);
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV4) {
        GenericRxMatchUdp(AF_INET, XDP_MATCH_UDP_PORT_SET);
    }

    TEST_METHOD(GenericRxMatchUdpPortSetV6) {
        GenericRxMatchUdp(AF_INET6, XDP_MATCH_UDP_PORT_SET);
    }

    TEST_METHOD(GenericRxMatchIpv4UdpPortSet) {
        GenericRxMatchUdp(AF_INET, XDP_MATCH_IPV4_UDP_PORT_SET);
    }

    TEST_METHOD(GenericRxMatchIpv6UdpPortSet) {
        GenericRxMatchUdp(AF_INET6, XDP_MATCH_IPV6_UDP_PORT_SET);
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
        GenericRxUdpFragmentQuicLongHeader(AF_INET);
    }

    TEST_METHOD(GenericRxUdpFragmentQuicLongHeaderV6) {
        GenericRxUdpFragmentQuicLongHeader(AF_INET6);
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV4) {
        GenericRxUdpFragmentQuicShortHeader(AF_INET);
    }

    TEST_METHOD(GenericRxUdpFragmentQuicShortHeaderV6) {
        GenericRxUdpFragmentQuicShortHeader(AF_INET6);
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV4) {
        GenericRxUdpFragmentHeaderData(AF_INET);
    }

    TEST_METHOD(GenericRxUdpFragmentHeaderDataV6) {
        GenericRxUdpFragmentHeaderData(AF_INET6);
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV4) {
        GenericRxUdpTooManyFragments(AF_INET);
    }

    TEST_METHOD(GenericRxUdpTooManyFragmentsV6) {
        GenericRxUdpTooManyFragments(AF_INET6);
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV4) {
        GenericRxUdpHeaderFragments(AF_INET);
    }

    TEST_METHOD(GenericRxUdpHeaderFragmentsV6) {
        GenericRxUdpHeaderFragments(AF_INET6);
    }

    TEST_METHOD(GenericRxFromTxInspectV4) {
        GenericRxFromTxInspect(AF_INET);
    }

    TEST_METHOD(GenericRxFromTxInspectV6) {
        GenericRxFromTxInspect(AF_INET6);
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
};
