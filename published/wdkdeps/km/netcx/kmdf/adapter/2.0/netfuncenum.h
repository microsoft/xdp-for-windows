/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

_WdfVersionBuild_

Module Name: NetFuncEnum.h

Abstract:
    Generated an enum of all WDF API functions

Environment:
    kernel mode only

    Warning: manual changes to this file will be lost.
--*/

#ifndef _NETFUNCENUM_2_0_H_
#define _NETFUNCENUM_2_0_H_

extern PNET_DRIVER_GLOBALS NetDriverGlobals;

typedef enum _NETFUNCENUM {

    NetAdapterInitAllocateTableIndex = 0,
    NetAdapterInitFreeTableIndex = 1,
    NetAdapterInitSetDatapathCallbacksTableIndex = 2,
    NetAdapterCreateTableIndex = 3,
    NetAdapterStartTableIndex = 4,
    NetAdapterStopTableIndex = 5,
    NetAdapterSetLinkLayerCapabilitiesTableIndex = 6,
    NetAdapterSetLinkLayerMtuSizeTableIndex = 7,
    NetAdapterPowerOffloadSetArpCapabilitiesTableIndex = 8,
    NetAdapterPowerOffloadSetNSCapabilitiesTableIndex = 9,
    NetAdapterWakeSetBitmapCapabilitiesTableIndex = 10,
    NetAdapterWakeSetMediaChangeCapabilitiesTableIndex = 11,
    NetAdapterWakeSetMagicPacketCapabilitiesTableIndex = 12,
    NetAdapterWakeSetPacketFilterCapabilitiesTableIndex = 13,
    NetAdapterSetDataPathCapabilitiesTableIndex = 14,
    NetAdapterSetLinkStateTableIndex = 15,
    NetAdapterGetNetLuidTableIndex = 16,
    NetAdapterOpenConfigurationTableIndex = 17,
    NetAdapterSetPermanentLinkLayerAddressTableIndex = 18,
    NetAdapterSetCurrentLinkLayerAddressTableIndex = 19,
    NetAdapterOffloadSetChecksumCapabilitiesTableIndex = 20,
    NetOffloadIsChecksumIPv4EnabledTableIndex = 21,
    NetOffloadIsChecksumTcpEnabledTableIndex = 22,
    NetOffloadIsChecksumUdpEnabledTableIndex = 23,
    NetAdapterReportWakeReasonPacketTableIndex = 24,
    NetAdapterReportWakeReasonMediaChangeTableIndex = 25,
    NetAdapterInitGetCreatedAdapterTableIndex = 26,
    NetAdapterExtensionInitAllocateTableIndex = 27,
    NetAdapterExtensionInitSetOidRequestPreprocessCallbackTableIndex = 28,
    NetAdapterDispatchPreprocessedOidRequestTableIndex = 29,
    NetAdapterGetParentTableIndex = 30,
    NetAdapterGetLinkLayerMtuSizeTableIndex = 31,
    NetAdapterExtensionInitSetNdisPmCapabilitiesTableIndex = 32,
    NetAdapterWdmGetNdisHandleTableIndex = 33,
    NetAdapterDriverWdmGetHandleTableIndex = 34,
    NetConfigurationCloseTableIndex = 35,
    NetConfigurationOpenSubConfigurationTableIndex = 36,
    NetConfigurationQueryUlongTableIndex = 37,
    NetConfigurationQueryStringTableIndex = 38,
    NetConfigurationQueryMultiStringTableIndex = 39,
    NetConfigurationQueryBinaryTableIndex = 40,
    NetConfigurationQueryLinkLayerAddressTableIndex = 41,
    NetConfigurationAssignUlongTableIndex = 42,
    NetConfigurationAssignUnicodeStringTableIndex = 43,
    NetConfigurationAssignMultiStringTableIndex = 44,
    NetConfigurationAssignBinaryTableIndex = 45,
    NetDeviceInitConfigTableIndex = 46,
    NetDeviceOpenConfigurationTableIndex = 47,
    NetDeviceInitSetPowerPolicyEventCallbacksTableIndex = 48,
    NetDeviceInitSetResetConfigTableIndex = 49,
    NetDeviceAssignSupportedOidListTableIndex = 50,
    NetPowerOffloadGetTypeTableIndex = 51,
    NetPowerOffloadGetArpParametersTableIndex = 52,
    NetPowerOffloadGetNSParametersTableIndex = 53,
    NetDeviceGetPowerOffloadListTableIndex = 54,
    NetPowerOffloadListGetCountTableIndex = 55,
    NetPowerOffloadListGetElementTableIndex = 56,
    NetAdapterSetReceiveScalingCapabilitiesTableIndex = 57,
    NetRxQueueCreateTableIndex = 58,
    NetRxQueueNotifyMoreReceivedPacketsAvailableTableIndex = 59,
    NetRxQueueInitGetQueueIdTableIndex = 60,
    NetRxQueueGetRingCollectionTableIndex = 61,
    NetRxQueueGetExtensionTableIndex = 62,
    NetTxQueueCreateTableIndex = 63,
    NetTxQueueNotifyMoreCompletedPacketsAvailableTableIndex = 64,
    NetTxQueueInitGetQueueIdTableIndex = 65,
    NetTxQueueGetRingCollectionTableIndex = 66,
    NetTxQueueGetExtensionTableIndex = 67,
    NetWakeSourceGetTypeTableIndex = 68,
    NetWakeSourceGetAdapterTableIndex = 69,
    NetWakeSourceGetBitmapParametersTableIndex = 70,
    NetWakeSourceGetMediaChangeParametersTableIndex = 71,
    NetDeviceGetWakeSourceListTableIndex = 72,
    NetWakeSourceListGetCountTableIndex = 73,
    NetWakeSourceListGetElementTableIndex = 74,
    NetFunctionTableNumEntries = 75,
} NETFUNCENUM;

#endif // _NETFUNCENUM_2_0_H_

