//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <netiodef.h>
#include <ws2ipdef.h>
#include <mstcpip.h>
#include <winternl.h>
#include <pkthlp.h>
#include <stdio.h>
#include <stdlib.h>

CONST CHAR *UsageText =
"Usage: pktcmd udp|tcp EthSrc EthDst IpSrc IpDst PortSrc PortDst PayloadLength";

VOID
Usage(
    CHAR *Error
    )
{
    fprintf(stderr, "Error: %s\n%s", Error, UsageText);
}

INT
__cdecl
main(
    INT ArgC,
    CHAR **ArgV
    )
{
    INT Err = 0;
    ETHERNET_ADDRESS EthSrc, EthDst;
    INET_ADDR IpSrc, IpDst;
    UINT16 PortSrc, PortDst;
    ADDRESS_FAMILY Af, AfSrc, AfDst;
    UCHAR *PayloadBuffer;
    UCHAR *PacketBuffer;
    UINT32 PacketLength;
    UINT16 PayloadLength;
    CONST CHAR *Terminator;
    BOOLEAN IsUdp;

    if (ArgC < 9) {
        Usage("Missing parameter");
        Err = 1;
        goto Exit;
    }

    if (_stricmp("udp", ArgV[1]) == 0) {
        IsUdp = TRUE;
    } else if (_stricmp("tcp", ArgV[1]) == 0) {
        IsUdp = FALSE;
    } else{
        Usage("Unsupported protocol");
        Err = 1;
        goto Exit;
    }

    if (RtlEthernetStringToAddressA(ArgV[2], &Terminator, (DL_EUI48 *)&EthSrc)) {
        Usage("EthSrc");
        Err = 1;
        goto Exit;
    }

    if (RtlEthernetStringToAddressA(ArgV[3], &Terminator, (DL_EUI48 *)&EthDst)) {
        Usage("EthDst");
        Err = 1;
        goto Exit;
    }

    if (!PktStringToInetAddressA(&IpSrc, &AfSrc, ArgV[4])) {
        Usage("IpSrc");
        Err = 1;
        goto Exit;
    }

    if (!PktStringToInetAddressA(&IpDst, &AfDst, ArgV[5])) {
        Usage("IpSrc");
        Err = 1;
        goto Exit;
    }

    if (AfSrc != AfDst) {
        Usage("IP protocol mismatch");
        Err = 1;
        goto Exit;
    }

    Af = AfSrc;
    PortSrc = htons((UINT16)atoi(ArgV[6]));
    PortDst = htons((UINT16)atoi(ArgV[7]));
    PayloadLength = (UINT16)atoi(ArgV[8]);

    PayloadBuffer = calloc(1, PayloadLength > 0 ? PayloadLength : 1);
    if (PayloadBuffer == NULL) {
        Usage("Allocation failed");
        Err = 1;
        goto Exit;
    }

    if (IsUdp) {
        PacketLength = UDP_HEADER_BACKFILL(Af) + PayloadLength;
        __analysis_assume(PacketLength > UDP_HEADER_BACKFILL(Af));
    } else {
        PacketLength = TCP_HEADER_BACKFILL(Af) + PayloadLength;
        __analysis_assume(PacketLength > TCP_HEADER_BACKFILL(Af));
    }
    PacketBuffer = malloc(PacketLength);
    if (PacketBuffer == NULL) {
        Usage("Allocation failed");
        Err = 1;
        goto Exit;
    }

    if (IsUdp) {
        if (!PktBuildUdpFrame(
                PacketBuffer, &PacketLength, PayloadBuffer, PayloadLength, &EthDst, &EthSrc, Af, &IpDst, &IpSrc,
                PortDst, PortSrc)) {
            Usage("Failed to build the UDP packet");
            Err = 1;
            goto Exit;
        }
    } else {
        if (!PktBuildTcpFrame(
                PacketBuffer, &PacketLength, PayloadBuffer, PayloadLength,
                NULL, 0, 1, 2, TH_SYN | TH_ACK, 4, &EthDst, &EthSrc, Af, &IpDst, &IpSrc,
                PortDst, PortSrc)) {
            Usage("Failed to build the TCP packet");
            Err = 1;
            goto Exit;
        }
    }

    for (UINT32 Index = 0; Index < PacketLength; Index++) {
        printf("%02x", PacketBuffer[Index]);
    }

Exit:
    return Err;
}
