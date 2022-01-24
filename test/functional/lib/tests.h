//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

bool
TestSetup();

bool
TestCleanup();

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
GenericTxPoke();

VOID
GenericTxMtu();

VOID
GenericXskWait(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
    );

VOID
GenericLwfDelayDetach(
    _In_ BOOLEAN Rx,
    _In_ BOOLEAN Tx
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
OffloadRss();
