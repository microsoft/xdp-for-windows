//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

bool
TestSetup();

bool
TestCleanup();

VOID
OpenApiTest();

VOID
LoadApiTest();

VOID
GenericBinding();

VOID
GenericBindingResetAdapter();

VOID
GenericRxSingleFrame();

VOID
GenericRxNoPoke();

VOID
GenericRxBackfillAndTrailer();

VOID
GenericRxMatchUdp(
    _In_ ADDRESS_FAMILY Af,
    _In_ XDP_MATCH_TYPE MatchType
    );

VOID
GenericRxMatchIpPrefix(
    _In_ UINT16 AddressFamily
    );

VOID
GenericRxLowResources();

VOID
GenericRxMultiSocket();

VOID
GenericRxMultiProgram();

VOID
GenericRxMultiProgramConflicts();

VOID
GenericRxUdpFragmentQuicShortHeader(
    _In_ ADDRESS_FAMILY Af
    );

VOID
GenericRxUdpFragmentQuicLongHeader(
    _In_ ADDRESS_FAMILY Af
    );

VOID
GenericRxUdpFragmentHeaderData(
    _In_ ADDRESS_FAMILY Af
    );

VOID
GenericRxUdpTooManyFragments(
    _In_ ADDRESS_FAMILY Af
    );

VOID
GenericRxUdpHeaderFragments(
    _In_ ADDRESS_FAMILY Af
    );

VOID
GenericRxFromTxInspect(
    _In_ ADDRESS_FAMILY Af
    );

VOID
GenericTxToRxInject();

VOID
GenericTxSingleFrame();

VOID
GenericTxOutOfOrder();

VOID
GenericTxSharing();

VOID
GenericTxPoke();

VOID
GenericTxMtu();

VOID
GenericXskWait(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    );

VOID
GenericXskWaitAsync(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    );

VOID
GenericLwfDelayDetach(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    );

VOID
GenericLoopback(
    _In_ ADDRESS_FAMILY Af
    );

VOID
FnMpNativeHandleTest();

VOID
FnLwfRx();

VOID
FnLwfTx();

VOID
FnLwfOid();

VOID
OffloadRssError();

VOID
OffloadRssReference();

VOID
OffloadRssInterfaceRestart();

VOID
OffloadRssUnchanged();

VOID
OffloadRssUpperSet();

VOID
OffloadRssSet();

VOID
OffloadRssCapabilities();

VOID
OffloadSetHardwareCapabilities();

VOID
GenericXskQueryAffinity();
