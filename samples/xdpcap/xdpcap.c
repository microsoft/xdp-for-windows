//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <xdpapi.h>
#include <afxdp_helper.h>
#include <cxplat.h>

#ifdef _KERNEL_MODE
#define LOGERR(...)
#define LOGINFO(...)
#else
#include <stdio.h>
#include <stdlib.h>
#define LOGERR(...) \
    fprintf(stderr, "ERR: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
#define LOGINFO(...) \
    printf("INFO: "); printf(__VA_ARGS__); printf("\n")
#endif

const CHAR *UsageText =
"xdpcap.exe <IfIndex> [OPTIONS]\n"
"\n"
"Captures network traffic using AF_XDP sockets and prints packet statistics.\n"
"This sample demonstrates how to use XDP for packet monitoring and analysis.\n"
"\n"
"Arguments:\n"
"  IfIndex           Network interface index to capture from\n"
"\n"
"Options:\n"
"  -QueueId <Id>     Queue ID to capture from (default: 0)\n"
"  -Count <N>        Number of packets to capture (default: 100)\n"
"  -Verbose          Print detailed packet information\n"
"\n"
"Example:\n"
"  xdpcap.exe 12 -Count 1000 -Verbose\n"
;

const XDP_HOOK_ID XdpInspectRxL2 = {
    .Layer = XDP_HOOK_L2,
    .Direction = XDP_HOOK_RX,
    .SubLayer = XDP_HOOK_INSPECT,
};

typedef struct _PACKET_STATS {
    UINT64 TotalPackets;
    UINT64 TotalBytes;
    UINT64 EthernetPackets;
    UINT64 IpPackets;
    UINT64 TcpPackets;
    UINT64 UdpPackets;
    UINT64 IcmpPackets;
    UINT64 OtherPackets;
} PACKET_STATS;

static
VOID
PrintPacketInfo(
    _In_ UCHAR *Frame,
    _In_ UINT32 Length,
    _In_ BOOLEAN Verbose
    )
{
    if (Length < 14) {
        LOGINFO("Short frame (%u bytes)", Length);
        return;
    }

    // Parse Ethernet header
    UINT16 EtherType = ntohs(*(UINT16*)(Frame + 12));
    
    if (Verbose) {
        LOGINFO("Packet: %u bytes, EtherType: 0x%04x", Length, EtherType);
        
        // Print source and destination MAC addresses
        printf("  Src MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
               Frame[6], Frame[7], Frame[8], Frame[9], Frame[10], Frame[11]);
        printf("  Dst MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
               Frame[0], Frame[1], Frame[2], Frame[3], Frame[4], Frame[5]);
    }

    // Parse IP packets
    if (EtherType == 0x0800 && Length >= 34) { // IPv4
        UCHAR Protocol = Frame[23];
        
        if (Verbose) {
            // Print source and destination IP addresses
            printf("  Src IP: %u.%u.%u.%u\n", Frame[26], Frame[27], Frame[28], Frame[29]);
            printf("  Dst IP: %u.%u.%u.%u\n", Frame[30], Frame[31], Frame[32], Frame[33]);
            printf("  Protocol: %u", Protocol);
            
            switch (Protocol) {
                case 1: printf(" (ICMP)"); break;
                case 6: printf(" (TCP)"); break;
                case 17: printf(" (UDP)"); break;
            }
            printf("\n");
        }
    }
    
    if (Verbose) {
        printf("\n");
    }
}

static
VOID
PrintStats(
    _In_ PACKET_STATS *Stats
    )
{
    LOGINFO("=== Capture Statistics ===");
    LOGINFO("Total Packets: %llu", Stats->TotalPackets);
    LOGINFO("Total Bytes: %llu", Stats->TotalBytes);
    LOGINFO("Ethernet Packets: %llu", Stats->EthernetPackets);
    LOGINFO("IP Packets: %llu", Stats->IpPackets);
    LOGINFO("TCP Packets: %llu", Stats->TcpPackets);
    LOGINFO("UDP Packets: %llu", Stats->UdpPackets);
    LOGINFO("ICMP Packets: %llu", Stats->IcmpPackets);
    LOGINFO("Other Packets: %llu", Stats->OtherPackets);
}

static
VOID
UpdateStats(
    _In_ UCHAR *Frame,
    _In_ UINT32 Length,
    _Inout_ PACKET_STATS *Stats
    )
{
    Stats->TotalPackets++;
    Stats->TotalBytes += Length;
    
    if (Length >= 14) {
        Stats->EthernetPackets++;
        
        UINT16 EtherType = ntohs(*(UINT16*)(Frame + 12));
        if (EtherType == 0x0800 && Length >= 34) { // IPv4
            Stats->IpPackets++;
            
            UCHAR Protocol = Frame[23];
            switch (Protocol) {
                case 1: Stats->IcmpPackets++; break;
                case 6: Stats->TcpPackets++; break;
                case 17: Stats->UdpPackets++; break;
                default: Stats->OtherPackets++; break;
            }
        } else {
            Stats->OtherPackets++;
        }
    }
}

INT
main(
    INT argc,
    CHAR **argv
    )
{
    HRESULT Result;
    UINT32 IfIndex = 0;
    UINT32 QueueId = 0;
    UINT32 PacketCount = 100;
    BOOLEAN Verbose = FALSE;
    
    HANDLE Socket = NULL;
    HANDLE Program = NULL;
    XSK_RING RxRing, FillRing;
    XDP_RULE Rule = {0};
    PACKET_STATS Stats = {0};
    
    // Parse command line arguments
    if (argc < 2) {
        printf("%s", UsageText);
        return -1;
    }
    
    IfIndex = strtoul(argv[1], NULL, 0);
    
    for (INT i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-QueueId") == 0 && i + 1 < argc) {
            QueueId = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-Count") == 0 && i + 1 < argc) {
            PacketCount = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-Verbose") == 0) {
            Verbose = TRUE;
        } else {
            LOGERR("Unknown argument: %s", argv[i]);
            return -1;
        }
    }
    
    LOGINFO("Starting packet capture on interface %u, queue %u", IfIndex, QueueId);
    LOGINFO("Will capture %u packets", PacketCount);
    
    // Create AF_XDP socket
    Result = XskCreate(&Socket);
    if (XDP_FAILED(Result)) {
        LOGERR("XskCreate failed: %x", Result);
        goto Exit;
    }
    
    // Bind to interface and queue
    Result = XskBind(Socket, IfIndex, QueueId, XSK_BIND_FLAG_RX);
    if (XDP_FAILED(Result)) {
        LOGERR("XskBind failed: %x", Result);
        goto Exit;
    }
    
    // Get socket rings
    XSK_RING_INFO_SET InfoSet = {0};
    UINT32 InfoSetSize = sizeof(InfoSet);
    Result = XskGetSockopt(Socket, XSK_SOCKOPT_RING_INFO, &InfoSet, &InfoSetSize);
    if (XDP_FAILED(Result)) {
        LOGERR("XskGetSockopt failed: %x", Result);
        goto Exit;
    }
    
    XskRingInitialize(&RxRing, &InfoSet.Rx);
    XskRingInitialize(&FillRing, &InfoSet.Fill);
    
    // Fill the RX fill ring with buffers
    UINT32 FillIndex;
    if (XskRingProducerReserve(&FillRing, InfoSet.Fill.Size, &FillIndex) != InfoSet.Fill.Size) {
        LOGERR("Failed to reserve fill ring");
        goto Exit;
    }
    
    for (UINT32 i = 0; i < InfoSet.Fill.Size; i++) {
        XSK_BUFFER_DESCRIPTOR *FillBuffer = XskRingGetElement(&FillRing, FillIndex + i);
        FillBuffer->Address.BaseAddress = 0;
        FillBuffer->Address.Offset = i * 2048; // 2KB per buffer
    }
    XskRingProducerSubmit(&FillRing, InfoSet.Fill.Size);
    
    // Activate the socket
    Result = XskActivate(Socket, 0);
    if (XDP_FAILED(Result)) {
        LOGERR("XskActivate failed: %x", Result);
        goto Exit;
    }
    
    // Create XDP program to capture all packets
    Rule.Match = XDP_MATCH_ALL;
    Rule.Action = XDP_PROGRAM_ACTION_REDIRECT;
    Rule.Redirect.TargetType = XDP_REDIRECT_TARGET_TYPE_XSK;
    Rule.Redirect.Target = Socket;
    
    Result = XdpCreateProgram(IfIndex, &XdpInspectRxL2, 0, 0, &Rule, 1, &Program);
    if (XDP_FAILED(Result)) {
        LOGERR("XdpCreateProgram failed: %x", Result);
        goto Exit;
    }
    
    LOGINFO("Capture started. Press Ctrl+C to stop early...");
    
    // Capture packets
    UINT32 CapturedPackets = 0;
    while (CapturedPackets < PacketCount) {
        UINT32 RxIndex;
        UINT32 Reserved = XskRingConsumerReserve(&RxRing, 1, &RxIndex);
        
        if (Reserved > 0) {
            XSK_BUFFER_DESCRIPTOR *RxBuffer = XskRingGetElement(&RxRing, RxIndex);
            UCHAR *PacketData = (UCHAR*)RxBuffer->Address.BaseAddress + RxBuffer->Address.Offset;
            UINT32 PacketLength = RxBuffer->Length;
            
            // Process the packet
            UpdateStats(PacketData, PacketLength, &Stats);
            if (Verbose) {
                PrintPacketInfo(PacketData, PacketLength, TRUE);
            }
            
            // Return buffer to fill ring
            UINT32 FillIndex;
            if (XskRingProducerReserve(&FillRing, 1, &FillIndex) == 1) {
                XSK_BUFFER_DESCRIPTOR *FillBuffer = XskRingGetElement(&FillRing, FillIndex);
                *FillBuffer = *RxBuffer;
                XskRingProducerSubmit(&FillRing, 1);
            }
            
            XskRingConsumerRelease(&RxRing, 1);
            CapturedPackets++;
            
            if ((CapturedPackets % 100) == 0) {
                LOGINFO("Captured %u packets...", CapturedPackets);
            }
        } else {
            // No packets available, brief sleep
            Sleep(1);
        }
    }
    
    LOGINFO("Capture completed.");
    PrintStats(&Stats);

Exit:
    if (Program != NULL) {
        CloseHandle(Program);
    }
    if (Socket != NULL) {
        CloseHandle(Socket);
    }
    
    return XDP_SUCCEEDED(Result) ? 0 : -1;
}