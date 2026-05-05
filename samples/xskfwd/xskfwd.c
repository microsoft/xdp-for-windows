//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <xdpapi.h>
#include <afxdp_helper.h>
#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#pragma warning(pop)
#include <stdio.h>
#include <stdlib.h>

#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")

CONST CHAR *UsageText =
"xskfwd.exe <IfIndex> [-BpfProgram <path.sys>] [-ProgramName <name>]\n"
"\n"
"Forwards RX traffic using an eBPF XDP program and AF_XDP sockets. Traffic\n"
"destined to UDP port 1234 on the specified interface is redirected to an\n"
"AF_XDP socket, MAC addresses are swapped, and the frame is transmitted back.\n"
"\n"
"If -BpfProgram is not specified, loads xskfwd_redirect.sys from the current\n"
"directory. If -ProgramName is not specified, defaults to xskfwd_redirect.\n"
;

static const CHAR *DefaultProgramName = "xskfwd_redirect";

static const CHAR *DefaultBpfProgram = "xskfwd_redirect.sys";

static
VOID
TranslateRxToTx(
    _Inout_ UCHAR *Frame,
    _In_ UINT32 Length
    )
{
    UCHAR MacAddress[6];

    //
    // Swap source and destination MAC addresses to echo the frame back.
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
    HRESULT HResult;
    int Result = 1;
    UINT32 IfIndex;
    const CHAR *BpfProgramPath = DefaultBpfProgram;
    struct bpf_object *BpfObject = NULL;
    struct bpf_program *BpfProgram = NULL;
    int ProgramFd;
    fd_t XskMapFd;
    HANDLE Socket = NULL;
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
    UINT32 QueueId = 0;

    //
    // Parse arguments.
    //
    if (argc < 2 || argv[1][0] == '-') {
        printf("%s", UsageText);
        return 1;
    }

    IfIndex = atoi(argv[1]);
    if (IfIndex == 0) {
        LOGERR("Invalid IfIndex");
        return 1;
    }

    const CHAR *ProgramName = DefaultProgramName;

    for (INT i = 2; i < argc; i++) {
        if (!_stricmp(argv[i], "-BpfProgram")) {
            if (++i >= argc) {
                LOGERR("Missing BpfProgram path");
                return 1;
            }
            BpfProgramPath = argv[i];
        } else if (!_stricmp(argv[i], "-ProgramName")) {
            if (++i >= argc) {
                LOGERR("Missing ProgramName");
                return 1;
            }
            ProgramName = argv[i];
        }
    }

    HResult = XskCreate(&Socket);
    if (XDP_FAILED(HResult)) {
        LOGERR("XskCreate failed: %x", HResult);
        goto Exit;
    }

    //
    // Register a single-frame UMEM buffer.
    //
    UmemReg.TotalSize = sizeof(Frame);
    UmemReg.ChunkSize = sizeof(Frame);
    UmemReg.Address = Frame;

    HResult = XskSetSockopt(Socket, XSK_SOCKOPT_UMEM_REG, &UmemReg, sizeof(UmemReg));
    if (XDP_FAILED(HResult)) {
        LOGERR("XSK_UMEM_REG failed: %x", HResult);
        goto Exit;
    }

    //
    // Bind the socket to the interface and queue 0 for RX and TX.
    //
    HResult = XskBind(Socket, IfIndex, QueueId, XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX);
    if (XDP_FAILED(HResult)) {
        LOGERR("XskBind failed: %x", HResult);
        goto Exit;
    }

    //
    // Configure descriptor rings with capacity of one frame each.
    //
    HResult = XskSetSockopt(Socket, XSK_SOCKOPT_RX_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(HResult)) {
        LOGERR("XSK_SOCKOPT_RX_RING_SIZE failed: %x", HResult);
        goto Exit;
    }

    HResult = XskSetSockopt(Socket, XSK_SOCKOPT_RX_FILL_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(HResult)) {
        LOGERR("XSK_SOCKOPT_RX_FILL_RING_SIZE failed: %x", HResult);
        goto Exit;
    }

    HResult = XskSetSockopt(Socket, XSK_SOCKOPT_TX_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(HResult)) {
        LOGERR("XSK_SOCKOPT_TX_RING_SIZE failed: %x", HResult);
        goto Exit;
    }

    HResult = XskSetSockopt(Socket, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &RingSize, sizeof(RingSize));
    if (XDP_FAILED(HResult)) {
        LOGERR("XSK_SOCKOPT_TX_COMPLETION_RING_SIZE failed: %x", HResult);
        goto Exit;
    }

    HResult = XskActivate(Socket, XSK_ACTIVATE_FLAG_NONE);
    if (XDP_FAILED(HResult)) {
        LOGERR("XskActivate failed: %x", HResult);
        goto Exit;
    }

    OptionLength = sizeof(RingInfo);
    HResult = XskGetSockopt(Socket, XSK_SOCKOPT_RING_INFO, &RingInfo, &OptionLength);
    if (XDP_FAILED(HResult)) {
        LOGERR("XSK_SOCKOPT_RING_INFO failed: %x", HResult);
        goto Exit;
    }

    XskRingInitialize(&RxRing, &RingInfo.Rx);
    XskRingInitialize(&RxFillRing, &RingInfo.Fill);
    XskRingInitialize(&TxRing, &RingInfo.Tx);
    XskRingInitialize(&TxCompRing, &RingInfo.Completion);

    //
    // Place an empty frame descriptor into the RX fill ring.
    //
    XskRingProducerReserve(&RxFillRing, 1, &RingIndex);
    *(UINT64 *)XskRingGetElement(&RxFillRing, RingIndex) = 0;
    XskRingProducerSubmit(&RxFillRing, 1);

    BpfObject = bpf_object__open(BpfProgramPath);
    if (BpfObject == NULL) {
        LOGERR("bpf_object__open(%s) failed", BpfProgramPath);
        goto Exit;
    }

    Result = bpf_object__load(BpfObject);
    if (Result != 0) {
        LOGERR("bpf_object__load failed: %d", Result);
        goto Exit;
    }

    //
    // Find the redirect program and the XSKMAP.
    //
    BpfProgram = bpf_object__find_program_by_name(BpfObject, ProgramName);
    if (BpfProgram == NULL) {
        LOGERR("Program '%s' not found in %s", ProgramName, BpfProgramPath);
        goto Exit;
    }

    ProgramFd = bpf_program__fd(BpfProgram);
    if (ProgramFd < 0) {
        LOGERR("bpf_program__fd failed");
        goto Exit;
    }

    XskMapFd = bpf_object__find_map_fd_by_name(BpfObject, "xsk_map");
    if (XskMapFd < 0) {
        LOGERR("xsk_map not found in eBPF program");
        goto Exit;
    }

    //
    // Populate the XSKMAP: key = queue index, value = XSK socket handle.
    //
    Result = bpf_map_update_elem(XskMapFd, &QueueId, &Socket, 0);
    if (Result != 0) {
        LOGERR("bpf_map_update_elem(xsk_map) failed: %d", Result);
        goto Exit;
    }

    Result = bpf_xdp_attach(IfIndex, ProgramFd, 0, NULL);
    if (Result != 0) {
        LOGERR("bpf_xdp_attach(IfIndex=%u) failed: %d", IfIndex, Result);
        goto Exit;
    }

    printf("eBPF XSK forwarder attached to IfIndex %u (UDP port 1234). Ctrl+C to stop.\n", IfIndex);

    //
    // RX-to-TX forwarding loop: receive frames, swap MACs, and transmit.
    //
    for (;;) {
        if (XskRingConsumerReserve(&RxRing, 1, &RingIndex) == 1) {
            XSK_BUFFER_DESCRIPTOR *RxBuffer;
            XSK_BUFFER_DESCRIPTOR *TxBuffer;
            XSK_NOTIFY_RESULT_FLAGS NotifyResult;

            RxBuffer = XskRingGetElement(&RxRing, RingIndex);

            XskRingProducerReserve(&TxRing, 1, &RingIndex);
            TxBuffer = XskRingGetElement(&TxRing, RingIndex);

            TranslateRxToTx(
                &Frame[RxBuffer->Address.BaseAddress + RxBuffer->Address.Offset],
                RxBuffer->Length);

            *TxBuffer = *RxBuffer;

            XskRingConsumerRelease(&RxRing, 1);
            XskRingProducerSubmit(&TxRing, 1);

            HResult = XskNotifySocket(Socket, XSK_NOTIFY_FLAG_POKE_TX, 0, &NotifyResult);
            if (XDP_FAILED(HResult)) {
                LOGERR("XskNotifySocket failed: %x", HResult);
                goto Exit;
            }
        }

        if (XskRingConsumerReserve(&TxCompRing, 1, &RingIndex) == 1) {
            UINT64 *Tx = XskRingGetElement(&TxCompRing, RingIndex);
            UINT64 *Rx;

            XskRingProducerReserve(&RxFillRing, 1, &RingIndex);
            Rx = XskRingGetElement(&RxFillRing, RingIndex);
            *Rx = *Tx;

            XskRingConsumerRelease(&TxCompRing, 1);
            XskRingProducerSubmit(&RxFillRing, 1);
        }
    }

Exit:
    bpf_xdp_detach(IfIndex, 0, NULL);

    if (BpfObject != NULL) {
        bpf_object__close(BpfObject);
    }

    if (Socket != NULL) {
        CloseHandle(Socket);
    }

    return Result == 0 ? 0 : 1;
}
