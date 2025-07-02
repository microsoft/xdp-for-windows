//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <xdpapi.h>

#include <winsock2.h>

//
// Directly include some C++ headers that produce benign compiler warnings.
//
#pragma warning(push)
#pragma warning(disable:5252) // Multiple different types resulted in the same XFG type-hash D275361C54538B70; the PDB will only record information for one of them
#include <xlocnum>
#include <xlocale>
#pragma warning(pop)

#include <CppUnitTest.h>
#include <fntrace.h>

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

//
// Ensure our build system is defaulting to the latest supported API version.
//
C_ASSERT(XDP_API_VERSION == XDP_API_VERSION_LATEST);

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

    TEST_METHOD_PRERELEASE(GenericTxChecksumOffloadExtensions) {
        ::GenericTxChecksumOffloadExtensions();
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadExtensions) {
        ::GenericRxChecksumOffloadExtensions();
    }

    TEST_METHOD_PRERELEASE(GenericTxChecksumOffloadIp) {
        ::GenericTxChecksumOffloadIp();
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadIp) {
        ::GenericRxChecksumOffloadIp(FALSE);
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadIpWithRebind) {
        ::GenericRxChecksumOffloadIp(TRUE);
    }

    TEST_METHOD_PRERELEASE(GenericTxChecksumOffloadTcpV4) {
        ::GenericTxChecksumOffloadTcp(AF_INET);
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadTcpV4) {
        ::GenericRxChecksumOffloadTcp(AF_INET);
    }

    TEST_METHOD_PRERELEASE(GenericTxChecksumOffloadTcpV6) {
        ::GenericTxChecksumOffloadTcp(AF_INET6);
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadTcpV6) {
        ::GenericRxChecksumOffloadTcp(AF_INET6);
    }

    TEST_METHOD_PRERELEASE(GenericTxChecksumOffloadUdpV4) {
        ::GenericTxChecksumOffloadUdp(AF_INET);
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadUdpV4) {
        ::GenericRxChecksumOffloadUdp(AF_INET);
    }

    TEST_METHOD_PRERELEASE(GenericTxChecksumOffloadUdpV6) {
        ::GenericTxChecksumOffloadUdp(AF_INET6);
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadUdpV6) {
        ::GenericRxChecksumOffloadUdp(AF_INET6);
    }

    TEST_METHOD_PRERELEASE(GenericTxChecksumOffloadConfig) {
        ::GenericTxChecksumOffloadConfig();
    }

    TEST_METHOD_PRERELEASE(GenericRxChecksumOffloadConfig) {
        ::GenericRxChecksumOffloadConfig();
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

    TEST_METHOD(GenericRxMatchInnerIpPrefixV4Udp) {
        GenericRxMatchInnerIpPrefix(AF_INET);
    }

    TEST_METHOD(GenericRxMatchInnerIpPrefixV6Udp) {
        GenericRxMatchInnerIpPrefix(AF_INET6);
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

    TEST_METHOD(GenericRxInnerIpHeaderFragmentsV4) {
        GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, TRUE, FALSE, FALSE, FALSE, TRUE);
    }

    TEST_METHOD(GenericRxInnerIpHeaderFragmentsV6) {
        GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, TRUE, FALSE, FALSE, FALSE, TRUE);
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

    TEST_METHOD(GenericRxIpNextHeaderV4) {
        GenericRxHeaderFragments(AF_INET, XDP_PROGRAM_ACTION_REDIRECT, TRUE, FALSE, FALSE, TRUE);
    }

    TEST_METHOD(GenericRxIpNextHeaderV6) {
        GenericRxHeaderFragments(AF_INET6, XDP_PROGRAM_ACTION_REDIRECT, TRUE, FALSE, FALSE, TRUE);
    }

    TEST_METHOD(GenericRxFromTxInspectV4) {
        GenericRxFromTxInspect(AF_INET);
    }

    TEST_METHOD(GenericRxFromTxInspectV6) {
        GenericRxFromTxInspect(AF_INET6);
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
        ::SecurityAdjustDeviceAcl();
    }

    TEST_METHOD(EbpfNetsh) {
        ::EbpfNetsh();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfAttach) {
        ::GenericRxEbpfAttach();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfDrop) {
        ::GenericRxEbpfDrop();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfPass) {
        ::GenericRxEbpfPass();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfTx) {
        ::GenericRxEbpfTx();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfPayload) {
        ::GenericRxEbpfPayload();
    }

    TEST_METHOD_PRERELEASE(ProgTestRunRxEbpfPayload) {
        ::ProgTestRunRxEbpfPayload();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfIfIndex) {
        ::GenericRxEbpfIfIndex();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfFragments) {
        ::GenericRxEbpfFragments();
    }

    TEST_METHOD_PRERELEASE(GenericRxEbpfUnload) {
        ::GenericRxEbpfUnload();
    }

    TEST_METHOD(GenericLoopbackV4) {
        GenericLoopback(AF_INET);
    }

    TEST_METHOD(GenericLoopbackV6) {
        GenericLoopback(AF_INET6);
    }

    TEST_METHOD_PRERELEASE(OffloadRssError) {
        ::OffloadRssError();
    }

    TEST_METHOD_PRERELEASE(OffloadRssReference) {
        ::OffloadRssReference();
    }

    TEST_METHOD_PRERELEASE(OffloadRssInterfaceRestart) {
        ::OffloadRssInterfaceRestart();
    }

    TEST_METHOD_PRERELEASE(OffloadRssUnchanged) {
        ::OffloadRssUnchanged();
    }

    TEST_METHOD_PRERELEASE(OffloadRssUpperSet) {
        ::OffloadRssUpperSet();
    }

    TEST_METHOD_PRERELEASE(OffloadRssSet) {
        ::OffloadRssSet();
    }

    TEST_METHOD_PRERELEASE(OffloadRssCapabilities) {
        ::OffloadRssCapabilities();
    }

    TEST_METHOD_PRERELEASE(OffloadRssReset) {
        ::OffloadRssReset();
    }

    TEST_METHOD_PRERELEASE(OffloadSetHardwareCapabilities) {
        ::OffloadSetHardwareCapabilities();
    }

    TEST_METHOD(GenericXskQueryAffinity) {
        ::GenericXskQueryAffinity();
    }

    TEST_METHOD_PRERELEASE(OffloadQeoConnection) {
        ::OffloadQeoConnection();
    }

    TEST_METHOD_PRERELEASE(OffloadQeoRevertInterfaceRemoval) {
        ::OffloadQeoRevert(RevertReasonInterfaceRemoval);
    }

    TEST_METHOD_PRERELEASE(OffloadQeoRevertHandleClosure) {
        ::OffloadQeoRevert(RevertReasonHandleClosure);
    }

    TEST_METHOD_PRERELEASE(OffloadQeoOidFailure) {
        ::OffloadQeoOidFailure();
    }

    TEST_METHOD(OidPassthru) {
        ::OidPassthru();
    }
};
