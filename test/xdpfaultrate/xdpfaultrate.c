//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Measures the effective fault-injection rate seen by a simple, interface-
// independent XDP primitive: XSK creation + UMEM registration. It loops the
// primitive a fixed number of times and reports how often it succeeds. Run with
// XDP installed under fault injection (the XdpFaultInject registry value and/or
// driver verifier low-resources simulation, as spinxsk configures) to gauge the
// injection intensity in isolation from interface/datapath churn.
//

#define NOMINMAX
#include <xdp/wincommon.h>
#include <winsock2.h>
#include <afxdp_helper.h>
#include <xdpapi.h>
#include <stdio.h>
#include <stdlib.h>

int
__cdecl
main(
    int argc,
    char **argv
    )
{
    ULONG iterations = 10000;
    ULONG createOk = 0;
    ULONG umemOk = 0;
    ULONG createPct, umemPct, fullPct;

    for (int i = 1; i + 1 < argc; i += 2) {
        if (_stricmp(argv[i], "-Iterations") == 0) {
            iterations = strtoul(argv[i + 1], NULL, 0);
        }
    }
    if (iterations == 0) {
        iterations = 1;
    }

    for (ULONG i = 0; i < iterations; i++) {
        HANDLE sock = NULL;
        XSK_UMEM_REG umemReg = {0};
        HRESULT res;

        res = XskCreate(&sock);
        if (FAILED(res)) {
            continue;
        }
        createOk++;

        umemReg.TotalSize = 8 * 4096;
        umemReg.ChunkSize = 4096;
        umemReg.Address =
            VirtualAlloc(NULL, (SIZE_T)umemReg.TotalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (umemReg.Address != NULL) {
            res = XskSetSockopt(sock, XSK_SOCKOPT_UMEM_REG, &umemReg, sizeof(umemReg));
            if (SUCCEEDED(res)) {
                umemOk++;
            }
            VirtualFree(umemReg.Address, 0, MEM_RELEASE);
        }

        CloseHandle(sock);
    }

    createPct = createOk * 100 / iterations;
    umemPct = (createOk > 0) ? (umemOk * 100 / createOk) : 0;
    fullPct = umemOk * 100 / iterations;

    printf(
        "xdpfaultrate: iters=%lu XskCreate=%lu/%lu (%lu%%) UmemReg=%lu/%lu (%lu%% of created) "
        "full=%lu/%lu (%lu%%) injection~=%lu%%\n",
        iterations, createOk, iterations, createPct,
        umemOk, createOk, umemPct,
        umemOk, iterations, fullPct,
        100 - fullPct);

    return 0;
}
