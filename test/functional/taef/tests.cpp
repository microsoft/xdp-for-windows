//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#define INLINE_TEST_METHOD_MARKUP
#include <WexTestClass.h>
#include <winsock2.h>

#include "xdptest.h"
#include "tests.h"
#include "util.h"

#define XDPFNMP_SERVICE_NAME "xdpfnmp"

static CONST CHAR *PowershellPrefix;
static BOOLEAN XdpFnMpPreinstalled = TRUE;

//
// Test suite(s).
//

BOOLEAN
XdpFnMpInstall()
{
    CHAR CmdBuff[256];

    XdpFnMpPreinstalled = IsServiceInstalled(XDPFNMP_SERVICE_NAME);

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(CmdBuff, "%s .\\xdpfnmp.ps1 -Install %s", PowershellPrefix, XdpFnMpPreinstalled ? "-DriverPreinstalled" : "");
    TEST_EQUAL(0, system(CmdBuff));
    return TRUE;
}

BOOLEAN
XdpFnMpUninstall()
{
    CHAR CmdBuff[256];

    sprintf(CmdBuff, "%s  .\\xdpfnmp.ps1 -Uninstall %s", PowershellPrefix, XdpFnMpPreinstalled ? "-DriverPreinstalled" : "");
    system(CmdBuff);
    TEST_FALSE(IsServiceRunning(XDPFNMP_SERVICE_NAME));
    return TRUE;
}

bool
ModuleSetup()
{
    PowershellPrefix = GetPowershellPrefix();
    return XdpInstall() && XdpFnMpInstall() && TestSetup();
}

bool
ModuleCleanup()
{
    return !!TestCleanup() & !!XdpFnMpUninstall() & !!XdpUninstall();
}

MODULE_SETUP(ModuleSetup);
MODULE_CLEANUP(ModuleCleanup);

VOID
GenericRxMatchUdpV4()
{
    GenericRxMatchUdp(AF_INET);
}

VOID
GenericRxMatchUdpV6()
{
    GenericRxMatchUdp(AF_INET6);
}

VOID
GenericRxMatchIpPrefixV4()
{
    GenericRxMatchIpPrefix(AF_INET);
}

VOID
GenericRxMatchIpPrefixV6()
{
    GenericRxMatchIpPrefix(AF_INET6);
}

VOID
GenericXskWaitRx() {
    GenericXskWait(TRUE, FALSE);
}

VOID
GenericXskWaitTx() {
    GenericXskWait(FALSE, TRUE);
}

VOID
GenericXskWaitRxTx() {
    GenericXskWait(TRUE, TRUE);
}

VOID
GenericLwfDelayDetachRx() {
    GenericLwfDelayDetach(TRUE, FALSE);
}

VOID
GenericLwfDelayDetachTx() {
    GenericLwfDelayDetach(FALSE, TRUE);
}

VOID
GenericLwfDelayDetachRxTx() {
    GenericLwfDelayDetach(TRUE, TRUE);
}

VOID
GenericRxUdpFragmentHeaderDataV4()
{
    GenericRxUdpFragmentHeaderData(AF_INET);
}

VOID
GenericRxUdpFragmentHeaderDataV6()
{
    GenericRxUdpFragmentHeaderData(AF_INET6);
}

VOID
GenericRxUdpTooManyFragmentsV4()
{
    GenericRxUdpTooManyFragments(AF_INET);
}

VOID
GenericRxUdpTooManyFragmentsV6()
{
    GenericRxUdpTooManyFragments(AF_INET6);
}

VOID
GenericRxUdpHeaderFragmentsV4()
{
    GenericRxUdpHeaderFragments(AF_INET);
}

VOID
GenericRxUdpHeaderFragmentsV6()
{
    GenericRxUdpHeaderFragments(AF_INET6);
}

VOID
GenericRxFromTxInspectV4()
{
    GenericRxFromTxInspect(AF_INET);
}

VOID
GenericRxFromTxInspectV6()
{
    GenericRxFromTxInspect(AF_INET6);
}

class FunctionalTestSuite
{
    TEST_CLASS(FunctionalTestSuite)

    #define ADD_TEST(Priority, TestName)                        \
        TEST_METHOD(TestName)                                   \
        {                                                       \
            BEGIN_TEST_METHOD_PROPERTIES()                      \
                TEST_METHOD_PROPERTY(L"Priority", L#Priority)   \
            END_TEST_METHOD_PROPERTIES()                        \
            ::TestName();                                       \
        }                                                       \

    ADD_TEST(0, GenericBinding)
    ADD_TEST(1, GenericBindingResetAdapter)
    ADD_TEST(0, GenericRxSingleFrame)
    ADD_TEST(0, GenericRxNoPoke)
    ADD_TEST(0, GenericRxBackfillAndTrailer)
    ADD_TEST(0, GenericRxMatchUdpV4)
    ADD_TEST(0, GenericRxMatchUdpV6)
    ADD_TEST(0, GenericRxMatchIpPrefixV4)
    ADD_TEST(0, GenericRxMatchIpPrefixV6)
    ADD_TEST(0, GenericRxLowResources)
    ADD_TEST(0, GenericRxMultiSocket)
    ADD_TEST(0, GenericRxUdpFragmentHeaderDataV4)
    ADD_TEST(0, GenericRxUdpFragmentHeaderDataV6)
    ADD_TEST(0, GenericRxUdpTooManyFragmentsV4)
    ADD_TEST(0, GenericRxUdpTooManyFragmentsV6)
    ADD_TEST(0, GenericRxUdpHeaderFragmentsV4)
    ADD_TEST(0, GenericRxUdpHeaderFragmentsV6)
    ADD_TEST(0, GenericRxFromTxInspectV4)
    ADD_TEST(0, GenericRxFromTxInspectV6)
    ADD_TEST(0, GenericTxToRxInject)
    ADD_TEST(0, GenericTxSingleFrame)
    ADD_TEST(0, GenericTxOutOfOrder)
    ADD_TEST(0, GenericTxPoke)
    ADD_TEST(1, GenericTxMtu)
    ADD_TEST(1, GenericXskWaitRx)
    ADD_TEST(1, GenericXskWaitTx)
    ADD_TEST(1, GenericXskWaitRxTx)
    ADD_TEST(1, GenericLwfDelayDetachRx)
    ADD_TEST(1, GenericLwfDelayDetachTx)
    ADD_TEST(1, GenericLwfDelayDetachRxTx)

    ADD_TEST(0, FnMpNativeHandleTest)
};
