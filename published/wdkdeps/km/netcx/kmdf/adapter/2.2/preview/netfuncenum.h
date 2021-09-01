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

#ifndef _NETFUNCENUM_2_2_H_
#define _NETFUNCENUM_2_2_H_

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
    NetAdapterWakeSetEapolPacketCapabilitiesTableIndex = 75,
    NetAdapterSetReceiveFilterCapabilitiesTableIndex = 76,
    NetReceiveFilterGetPacketFilterTableIndex = 77,
    NetReceiveFilterGetMulticastAddressCountTableIndex = 78,
    NetReceiveFilterGetMulticastAddressListTableIndex = 79,
    NetDriverExtensionInitializeTableIndex = 80,
    NetAdapterExtensionInitSetDirectOidRequestPreprocessCallbackTableIndex = 81,
    NetExAdapterInitSetDirectOidPreprocessCallbackTableIndex = 82,
    NetAdapterExtensionInitSetTxPeerDemuxCallbackTableIndex = 83,
    NetAdapterDispatchPreprocessedDirectOidRequestTableIndex = 84,
    NetExAdapterDispatchPreprocessedDirectOidTableIndex = 85,
    NetAdapterExtensionSetNdisPmCapabilitiesTableIndex = 86,
    NetAdapterInitAllocateContextTableIndex = 87,
    NetAdapterInitGetTypedContextWorkerTableIndex = 88,
    NetAdapterOffloadSetTxChecksumCapabilitiesTableIndex = 89,
    NetOffloadIsTxChecksumIPv4EnabledTableIndex = 90,
    NetOffloadIsTxChecksumTcpEnabledTableIndex = 91,
    NetOffloadIsTxChecksumUdpEnabledTableIndex = 92,
    NetAdapterOffloadSetRxChecksumCapabilitiesTableIndex = 93,
    NetOffloadIsRxChecksumIPv4EnabledTableIndex = 94,
    NetOffloadIsRxChecksumTcpEnabledTableIndex = 95,
    NetOffloadIsRxChecksumUdpEnabledTableIndex = 96,
    NetAdapterOffloadSetGsoCapabilitiesTableIndex = 97,
    NetOffloadIsLsoIPv4EnabledTableIndex = 98,
    NetOffloadIsLsoIPv6EnabledTableIndex = 99,
    NetOffloadIsUsoIPv4EnabledTableIndex = 100,
    NetOffloadIsUsoIPv6EnabledTableIndex = 101,
    NetAdapterOffloadSetRscCapabilitiesTableIndex = 102,
    NetOffloadIsTcpRscIPv4EnabledTableIndex = 103,
    NetOffloadIsTcpRscIPv6EnabledTableIndex = 104,
    NetOffloadIsRscTcpTimestampOptionEnabledTableIndex = 105,
    NetAdapterOffloadSetIeee8021qTagCapabilitiesTableIndex = 106,
    NetAdapterInitAddTxDemuxTableIndex = 107,
    NetAdapterGetTxPeerAddressDemuxTableIndex = 108,
    NetBufferQueueCreateTableIndex = 109,
    NetBufferQueueGetExtensionTableIndex = 110,
    NetBufferQueueGetRingTableIndex = 111,
    NetDeviceInitSetResetCapabilitiesTableIndex = 112,
    NetDeviceStoreResetDiagnosticsTableIndex = 113,
    NetDeviceRequestResetTableIndex = 114,
    NetExecutionContextCreateTableIndex = 115,
    NetExecutionContextTaskCreateTableIndex = 116,
    NetExecutionContextNotifyTableIndex = 117,
    NetExecutionContextTaskEnqueueTableIndex = 118,
    NetExecutionContextTaskWaitCompletionTableIndex = 119,
    NetPowerOffloadGetAdapterTableIndex = 120,
    NetTxQueueGetDemux8021pTableIndex = 121,
    NetAdapterExtensionInitSetPowerPolicyCallbacksTableIndex = 122,
    NetAdapterInitSetSelfManagedPowerReferencesTableIndex = 123,
    NetAdapterWifiDestroyPeerAddressDatapathTableIndex = 124,
    NetAdapterPauseOffloadCapabilitiesTableIndex = 125,
    NetAdapterResumeOffloadCapabilitiesTableIndex = 126,
    NetTxQueueGetDemuxWmmInfoTableIndex = 127,
    NetTxQueueGetDemuxPeerAddressTableIndex = 128,
    NetFunctionTableNumEntries = 129,
} NETFUNCENUM;

#endif // _NETFUNCENUM_2_2_H_

