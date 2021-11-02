//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include <winsock2.h>
#include <CppUnitTest.h>

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

    TEST_METHOD(GenericTxToRxInject) {
        ::GenericTxToRxInject();
    }

    TEST_METHOD(GenericTxSingleFrame) {
        ::GenericTxSingleFrame();
    }

    TEST_METHOD(GenericTxOutOfOrder) {
        ::GenericTxOutOfOrder();
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
        GenericRxMatchUdp(AF_INET);
    }

    TEST_METHOD(GenericRxMatchUdpV6) {
        GenericRxMatchUdp(AF_INET6);
    }

    TEST_METHOD(GenericRxMatchIpPrefixV4) {
        GenericRxMatchIpPrefix(AF_INET);
    }

    TEST_METHOD(GenericRxMatchIpPrefixV6) {
        GenericRxMatchIpPrefix(AF_INET6);
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

    TEST_METHOD(GenericLwfDelayDetachRx) {
        GenericLwfDelayDetach(TRUE, FALSE);
    }

    TEST_METHOD(GenericLwfDelayDetachTx) {
        GenericLwfDelayDetach(FALSE, TRUE);
    }

    TEST_METHOD(GenericLwfDelayDetachRxTx) {
        GenericLwfDelayDetach(TRUE, TRUE);
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

#if DBG
    TEST_METHOD(OffloadRss) {
        ::OffloadRss();
    }
#endif
};
