//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <xdpapi.h>
#include <afxdp_helper.h>

const CHAR *UsageText =
"xskfwd.exe <IfIndex>"
"\n"
"Forwards RX traffic using an XDP program and AF_XDP sockets. This sample\n"
"application forwards traffic on the specified IfIndex originally destined to\n"
"UDP port 1234 back to the sender. Only the 0th data path queue on the interface\n"
"is used.\n"
;

const XDP_HOOK_ID XdpInspectRxL2 = {
    .Layer = XDP_HOOK_L2,
    .Direction = XDP_HOOK_RX,
    .SubLayer = XDP_HOOK_INSPECT,
};

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

static
VOID
TranslateRxToTx(
    _Inout_ UCHAR *Frame,
    _In_ UINT32 Length
    )
{
    UCHAR MacAddress[6];

    //
    // This function echo a UDP datagram back to its sender at the Ethernet
    // layer by swapping source and destination MAC addresses. The IP and UDP
    // layer headers are left as-is.
    //

    if (Length >= sizeof(MacAddress) * 2) {
        RtlCopyMemory(MacAddress, Frame, sizeof(MacAddress));
        RtlCopyMemory(Frame, Frame + sizeof(MacAddress), sizeof(MacAddress));
        RtlCopyMemory(Frame + sizeof(MacAddress), MacAddress, sizeof(MacAddress));
    }
}

INT
__cdecl
main(
    INT argc,
    CHAR **argv
    )
{
    const XDP_API_TABLE *XdpApi;
    HRESULT Result;
    HANDLE Socket;
    HANDLE Program;
    UINT32 IfIndex;
    XDP_RULE Rule = {0};
    UCHAR Frame[1514];
    XSK_UMEM_REG UmemReg = {0};
    const UINT32 RingSize = 1;
    XSK_RING_INFO_SET RingInfo;
    UINT32 OptionLength;
    XSK_RING RxRing;
    XSK_RING RxFillRing;
    XSK_RING TxRing;
    XSK_RING TxCompRing;
    UINT32 RingIndex;

    if (argc < 2) {
        fprintf(stderr, UsageText);
        return EXIT_FAILURE;
    }

    IfIndex = atoi(argv[1]);

    //
    // Retrieve the XDP API dispatch table.
    //
    Result = XdpOpenApi(XDP_API_VERSION_1, &XdpApi);
    if (FAILED(Result)) {
        LOGERR("XdpOpenApi failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Create an AF_XDP socket. The newly created socket is not connected.
    //
    Result = XdpApi->XskCreate(&Socket);
    if (FAILED(Result)) {
        LOGERR("XskCreate failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Register our frame buffer(s) with the AF_XDP socket. For simplicity, we
    // register a buffer containing a single frame. The registered buffer is
    // available mapped into AF_XDP's address space, and elements of descriptor
    // rings refer to relative offets from the start of the UMEM.
    //
    UmemReg.TotalSize = sizeof(Frame);
    UmemReg.ChunkSize = sizeof(Frame);
    UmemReg.Address = Frame;

    Result = XdpApi->XskSetSockopt(Socket, XSK_SOCKOPT_UMEM_REG, &UmemReg, sizeof(UmemReg));
    if (FAILED(Result)) {
        LOGERR("XSK_UMEM_REG failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Bind the AF_XDP socket to the specified interface and 0th data path
    // queue, and indicate the intent to perform RX and TX actions.
    //
    Result = XdpApi->XskBind(Socket, IfIndex, 0, XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX);
    if (FAILED(Result)) {
        LOGERR("XskBind failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Request a set of RX, RX fill, TX, and TX completion descriptor rings.
    // Request a capacity of one frame in each ring for simplicity. XDP will
    // create the rings and map them into the process address space as part of
    // the XskActivate step further below.
    //

    Result = XdpApi->XskSetSockopt(Socket, XSK_SOCKOPT_RX_RING_SIZE, &RingSize, sizeof(RingSize));
    if (FAILED(Result)) {
        LOGERR("XSK_SOCKOPT_RX_RING_SIZE failed: %x", Result);
        return EXIT_FAILURE;
    }

    Result = XdpApi->XskSetSockopt(Socket, XSK_SOCKOPT_RX_FILL_RING_SIZE, &RingSize, sizeof(RingSize));
    if (FAILED(Result)) {
        LOGERR("XSK_SOCKOPT_RX_FILL_RING_SIZE failed: %x", Result);
        return EXIT_FAILURE;
    }

    Result = XdpApi->XskSetSockopt(Socket, XSK_SOCKOPT_TX_RING_SIZE, &RingSize, sizeof(RingSize));
    if (FAILED(Result)) {
        LOGERR("XSK_SOCKOPT_TX_RING_SIZE failed: %x", Result);
        return EXIT_FAILURE;
    }

    Result = XdpApi->XskSetSockopt(Socket, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &RingSize, sizeof(RingSize));
    if (FAILED(Result)) {
        LOGERR("XSK_SOCKOPT_TX_COMPLETION_RING_SIZE failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Activate the AF_XDP socket. Once activated, descriptor rings are
    // available and RX and TX can occur.
    //
    Result = XdpApi->XskActivate(Socket, XSK_ACTIVATE_FLAG_NONE);
    if (FAILED(Result)) {
        LOGERR("XskActivate failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Retrieve the RX, RX fill, TX, and TX completion ring info from AF_XDP.
    //
    OptionLength = sizeof(RingInfo);
    Result = XdpApi->XskGetSockopt(Socket, XSK_SOCKOPT_RING_INFO, &RingInfo, &OptionLength);
    if (FAILED(Result)) {
        LOGERR("XSK_SOCKOPT_RING_INFO failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Initialize the optional AF_XDP helper library with the socket ring info.
    // These helpers simplify manipulation of the shared rings.
    //
    XskRingInitialize(&RxRing, &RingInfo.Rx);
    XskRingInitialize(&RxFillRing, &RingInfo.Fill);
    XskRingInitialize(&TxRing, &RingInfo.Tx);
    XskRingInitialize(&TxCompRing, &RingInfo.Completion);

    //
    // Place an empty frame descriptor into the RX fill ring. When the AF_XDP
    // socket receives a frame from XDP, it will pop the first available
    // frame descriptor from the RX fill ring and copy the frame payload into
    // that descriptor's buffer.
    //
    XskRingProducerReserve(&RxFillRing, 1, &RingIndex);

    //
    // The value of each RX fill and TX completion ring element is an offset
    // from the start of the UMEM to the start of the frame. Since this sample
    // is using a single buffer, the offset is always zero.
    //
    *(UINT32 *)XskRingGetElement(&RxFillRing, RingIndex) = 0;

    XskRingProducerSubmit(&RxFillRing, 1);

    //
    // Create an XDP program using the parsed rule at the L2 inspect hook point.
    // The rule intercepts all UDP frames destined to local port 1234 and
    // redirects them to the AF_XDP socket.
    //

    Rule.Match = XDP_MATCH_UDP_DST;
    Rule.Pattern.Port = 53764; // htons(1234)
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Socket;

    Result = XdpApi->XdpCreateProgram(IfIndex, &XdpInspectRxL2, 0, 0, &Rule, 1, &Program);
    if (FAILED(Result)) {
        LOGERR("XdpCreateProgram failed: %x", Result);
        return EXIT_FAILURE;
    }

    //
    // Continuously scan the RX ring and TX completion ring for new descriptors.
    // For simplicity, this loop performs actions one frame at a time. This can
    // be optimized further by consuming, reserving, and submitting batches of
    // frames across each XskRing* function.
    //
    while (TRUE) {
        if (XskRingConsumerReserve(&RxRing, 1, &RingIndex) == 1) {
            XSK_BUFFER_DESCRIPTOR *RxBuffer;
            XSK_BUFFER_DESCRIPTOR *TxBuffer;
            XSK_NOTIFY_RESULT_FLAGS NotifyResult;

            //
            // A new RX frame appeared on the RX ring. Forward it to the TX
            // ring.

            RxBuffer = XskRingGetElement(&RxRing, RingIndex);

            //
            // Reserve space in the TX ring. Since we're only using one frame in
            // this sample, space is guaranteed to be available.
            //
            XskRingProducerReserve(&TxRing, 1, &RingIndex);
            TxBuffer = XskRingGetElement(&TxRing, RingIndex);

            //
            // Swap source and destination fields within the frame payload.
            //
            TranslateRxToTx(
                &Frame[RxBuffer->Address.BaseAddress + RxBuffer->Address.Offset],
                RxBuffer->Length);

            //
            // Since the RX and TX buffer descriptor formats are identical,
            // simply copy the descriptor across rings.
            //
            *TxBuffer = *RxBuffer;

            //
            // Advance the consumer index of the RX ring and the producer index
            // of the TX ring, which allows XDP to write and read the descriptor
            // elements respectively.
            //
            XskRingConsumerRelease(&RxRing, 1);
            XskRingProducerSubmit(&TxRing, 1);

            //
            // Notify XDP that a new element is available on the TX ring, since
            // XDP isn't continuously checking the shared ring. This can be
            // optimized further using the XskRingProducerNeedPoke helper.
            //
            Result = XdpApi->XskNotifySocket(Socket, XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
            if (FAILED(Result)) {
                LOGERR("XskNotifySocket failed: %x", Result);
                return EXIT_FAILURE;
            }
        }

        if (XskRingConsumerReserve(&TxCompRing, 1, &RingIndex) == 1) {
            UINT32 *Tx;
            UINT32 *Rx;

            //
            // A TX frame address appeared on the TX completion ring. Recycle
            // the frame onto the RX fill ring.
            //

            Tx = XskRingGetElement(&TxCompRing, RingIndex);

            //
            // Reserve space in the RX fill ring. Since we're only using one
            // frame in this sample, space is guaranteed to be available.
            //
            XskRingProducerReserve(&RxFillRing, 1, &RingIndex);
            Rx = XskRingGetElement(&RxFillRing, RingIndex);

            //
            // Since the TX completion and RX fill descriptor formats are
            // identical, simply copy the descriptor across rings.
            //
            *Rx = *Tx;

            //
            // Advance the consumer index of the RX ring and the producer index
            // of the TX ring, which allows XDP to write and read the descriptor
            // elements respectively.
            //
            XskRingConsumerRelease(&TxCompRing, 1);
            XskRingProducerSubmit(&RxFillRing, 1);
        }
    }

    //
    // Close the XDP program. Traffic will no longer be intercepted by XDP.
    //
    CloseHandle(Program);

    //
    // Close the AF_XDP socket. All socket resources will be cleaned up by XDP.
    //
    CloseHandle(Socket);

    return EXIT_SUCCESS;
}
