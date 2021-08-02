//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <iphlpapi.h>
#define INLINE_TEST_METHOD_MARKUP
#include <WexTestClass.h>
#include <wil/resource.h>

#include "xdptest.h"
#include "util.h"

#define XDP_SERVICE_NAME "xdp"
#define XDPMP_SERVICE_NAME "xdpmp"
#define XDPMP_ADAPTER_ALIAS L"XDPMP"
#define FNDIS_SERVICE_NAME "fndis"
#define XDPMPF_SERVICE_NAME "xdpmpf"
#define XDPMPF_ADAPTER_ALIAS L"XDPMPF"
#define DEFAULT_DURATION_MINUTES 2

static CONST CHAR *PowershellPrefix;
static bool Fndis = FALSE;
static BOOLEAN XdpMpPreinstalled = TRUE;
static WEX::TestExecution::SetVerifyOutput
    s_verifyOuput(WEX::TestExecution::VerifyOutputSettings::Setting::LogOnlyFailures);

BOOLEAN
XdpMpInstall()
{
    CHAR CmdBuff[256];

    XdpMpPreinstalled = IsServiceInstalled(Fndis ? XDPMPF_SERVICE_NAME : XDPMP_SERVICE_NAME);

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(
        CmdBuff, "%s .\\xdpmp.ps1 -Install %s %s",
        PowershellPrefix, Fndis ? "-Fndis" : "", XdpMpPreinstalled ? "-DriverPreinstalled" : "");
    TEST_EQUAL(0, system(CmdBuff));
    return TRUE;
}

BOOLEAN
XdpMpUninstall()
{
    CHAR CmdBuff[256];

    sprintf(
        CmdBuff, "%s .\\xdpmp.ps1 -Uninstall %s %s",
        PowershellPrefix, Fndis ? "-Fndis" : "", XdpMpPreinstalled ? "-DriverPreinstalled" : "");
    system(CmdBuff);
    TEST_FALSE(IsServiceRunning(Fndis ? XDPMPF_SERVICE_NAME : XDPMP_SERVICE_NAME));
    return TRUE;
}

BOOLEAN
FndisInstall()
{
    CHAR CmdBuff[256];

    if (Fndis) {
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(CmdBuff, "%s .\\fndis.ps1 -Install", PowershellPrefix);
        TEST_EQUAL(0, system(CmdBuff));
    }

    return TRUE;
}

BOOLEAN
FndisUninstall()
{
    CHAR CmdBuff[256];

    if (Fndis) {
        sprintf_s(CmdBuff, "%s .\\fndis.ps1 -Uninstall", PowershellPrefix);
        system(CmdBuff);
        TEST_FALSE(IsServiceRunning(FNDIS_SERVICE_NAME));
    }

    return TRUE;
}

bool
ModuleSetup()
{
    CHAR CmdBuff[256];
    SYSTEM_INFO SystemInfo;

    WEX::TestExecution::RuntimeParameters::TryGetValue(L"Fndis", Fndis);
    PowershellPrefix = GetPowershellPrefix();
    if (!(XdpInstall() && FndisInstall() && XdpMpInstall())) {
        return FALSE;
    }

    //
    // Ensure we have at least 4 LPs.
    // Our expected test automation environment is a 4VP VM.
    //
    GetSystemInfo(&SystemInfo);
    TEST_TRUE(SystemInfo.dwNumberOfProcessors >= 4);

    RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
    sprintf_s(
        CmdBuff, "%s Set-NetAdapterRss -Name %ls -NumberOfReceiveQueues 2 -Profile NUMAStatic -BaseProcessorNumber 0 -MaxProcessorNumber 3 -MaxProcessors 2 -Enabled $true",
        PowershellPrefix, Fndis ? XDPMPF_ADAPTER_ALIAS : XDPMP_ADAPTER_ALIAS);
    TEST_EQUAL(0, system(CmdBuff));

    return TRUE;
}

bool
ModuleCleanup()
{
    return !!XdpMpUninstall() & !!FndisUninstall() & !!XdpUninstall();
}

MODULE_SETUP(ModuleSetup);
MODULE_CLEANUP(ModuleCleanup);

struct spinxsk {

    BEGIN_TEST_CLASS(spinxsk)
        TEST_CLASS_PROPERTY(L"RunAs", L"ElevatedUserOrSystem")
    END_TEST_CLASS()

    TEST_METHOD(run) {
        CHAR CmdBuff[256];
        UINT32 Minutes = DEFAULT_DURATION_MINUTES;
        ULONG IfIndex;
        WEX::TestExecution::RuntimeParameters::TryGetValue(L"Minutes", Minutes);
        VERIFY_ARE_EQUAL((DWORD)0, ConvertInterfaceAliasToIndex(Fndis ? XDPMPF_ADAPTER_ALIAS : XDPMP_ADAPTER_ALIAS, &IfIndex));
        VERIFY_ARE_NOT_EQUAL(-1, sprintf_s(CmdBuff, "spinxsk.exe -IfIndex %u -Stats -QueueCount 2 -Minutes %d > spinxsk.txt", IfIndex, Minutes));
        VERIFY_ARE_EQUAL(0, system(CmdBuff));
    }
};
