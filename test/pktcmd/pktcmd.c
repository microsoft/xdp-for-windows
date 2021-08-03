//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <mstcpip.h>
#include <pkthlp.h>
#include <stdio.h>
#include <stdlib.h>

CONST CHAR *UsageText =
"Usage: pktcmd udp EthSrc EthDst IpSrc IpDst PortSrc PortDst PayloadLength";

VOID
Usage(
    CHAR *Error
    )
{
    fprintf(stderr, "Error: %s\n%s", Error, UsageText);
    exit(1);
}

INT
__cdecl
main(
    INT ArgC,
    CHAR **ArgV
    )
{
    ETHERNET_ADDRESS EthSrc, EthDst;
    INET_ADDR IpSrc, IpDst;
    UINT16 PortSrc, PortDst;
    ADDRESS_FAMILY Af, AfSrc, AfDst;
    UCHAR *PayloadBuffer;
    UCHAR *UdpBuffer;
    UINT32 UdpLength;
    UINT16 PayloadLength;
    CONST CHAR *Terminator;

    if (ArgC < 9) {
        Usage("Missing parameter");
    }

    if (_stricmp("udp", ArgV[1])) {
        Usage("Unsupported protocol");
    }

    if (RtlEthernetStringToAddressA(ArgV[2], &Terminator, (DL_EUI48 *)&EthSrc)) {
        Usage("EthSrc");
    }

    if (RtlEthernetStringToAddressA(ArgV[3], &Terminator, (DL_EUI48 *)&EthDst)) {
        Usage("EthDst");
    }

    if (!PktStringToInetAddressA(&IpSrc, &AfSrc, ArgV[4])) {
        Usage("IpSrc");
    }

    if (!PktStringToInetAddressA(&IpDst, &AfDst, ArgV[5])) {
        Usage("IpSrc");
    }

    if (AfSrc != AfDst) {
        Usage("IP protocol mismatch");
    }

    Af = AfSrc;
    PortSrc = htons((UINT16)atoi(ArgV[6]));
    PortDst = htons((UINT16)atoi(ArgV[7]));
    PayloadLength = (UINT16)atoi(ArgV[8]);

    PayloadBuffer = calloc(1, PayloadLength > 0 ? PayloadLength : 1);
    if (PayloadBuffer == NULL) {
        Usage("Allocation failed");
    }

    UdpLength = UDP_HEADER_BACKFILL(Af) + PayloadLength;
    UdpBuffer = malloc(UdpLength);
    if (UdpBuffer == NULL) {
        Usage("Allocation failed");
    }

    if (!PktBuildUdpFrame(
        UdpBuffer, &UdpLength, PayloadBuffer, PayloadLength, &EthDst, &EthSrc, Af, &IpDst, &IpSrc,
        PortDst, PortSrc)) {
        Usage("Failed to build packet");
    }

    for (UINT32 Index = 0; Index < UDP_HEADER_BACKFILL(Af); Index++) {
        printf("%02x", UdpBuffer[Index]);
    }

    return 0;
}