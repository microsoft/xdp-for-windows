//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
#define NOMINMAX
#include <xdp/wincommon.h>
#include <winsock2.h>
#include <afxdp_helper.h>
#include <xdpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

typedef enum {
    StageCreate,
    StageAllocate,
    StageUmem,
    StageRxHook,
    StageTxHook,
    StageRxRing,
    StageFillRing,
    StageTxRing,
    StageCompletionRing,
    StageBind,
    StageActivate,
    StageCount
} PROBE_STAGE;

const char *stageNames[StageCount] = {
    "create", "virtual-alloc", "umem", "rx-hook", "tx-hook", "rx-ring",
    "fill-ring", "tx-ring", "completion-ring", "bind", "activate"
};

typedef struct {
    HRESULT Result;
    double Microseconds;
} SAMPLE;

typedef struct {
    const char *Name;
    XSK_BIND_FLAGS Flags;
    BOOLEAN Activate;
} PROBE_CASE;

const PROBE_CASE cases[] = {
    {"control", XSK_BIND_FLAG_NONE, FALSE},
    {"rx-bind", XSK_BIND_FLAG_RX, FALSE},
    {"rx-activate", XSK_BIND_FLAG_RX, TRUE},
    {"tx-bind", XSK_BIND_FLAG_TX, FALSE},
    {"tx-activate", XSK_BIND_FLAG_TX, TRUE},
    {"rxtx-bind", XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX, FALSE},
    {"rxtx-activate", XSK_BIND_FLAG_RX | XSK_BIND_FLAG_TX, TRUE}
};

int __cdecl
CompareResult(const void *Left, const void *Right)
{
    ULONG left = (ULONG)((const SAMPLE *)Left)->Result;
    ULONG right = (ULONG)((const SAMPLE *)Right)->Result;
    return (left > right) - (left < right);
}

int __cdecl
CompareLatency(const void *Left, const void *Right)
{
    double left = ((const SAMPLE *)Left)->Microseconds;
    double right = ((const SAMPLE *)Right)->Microseconds;
    return (left > right) - (left < right);
}

BOOLEAN
RunCase(const PROBE_CASE *Case, ULONG Iterations, ULONG IfIndex, ULONG QueueId)
{
    SAMPLE *samples = calloc((SIZE_T)StageCount * Iterations, sizeof(*samples));
    ULONG counts[StageCount] = {0};
    ULONG failures[StageCount] = {0};
    ULONG complete = 0;
    ULONG bucketAttempts = 0;
    ULONG bucketSuccess = 0;
    LARGE_INTEGER frequency, caseStart, bucketStart, now;
    FILETIME utc;
    BOOLEAN valid = TRUE;

    if (samples == NULL) {
        fprintf(stderr, "Unable to allocate diagnostic samples\n");
        return FALSE;
    }
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&caseStart);
    GetSystemTimePreciseAsFileTime(&utc);
    bucketStart = caseStart;
    printf("xdpfaultrate: case=%s ifindex=%lu queue=%lu flags=0x%x iterations=%lu\n",
        Case->Name, IfIndex, QueueId,
        Case->Flags == 0 ? 0 : Case->Flags | XSK_BIND_FLAG_GENERIC, Iterations);
    printf("xdpfaultrate: case=%s pid=%lu qpc=%lld frequency=%lld utc-filetime=%llu ring-size=8 umem-bytes=32768\n",
        Case->Name, GetCurrentProcessId(), caseStart.QuadPart, frequency.QuadPart,
        ((ULONGLONG)utc.dwHighDateTime << 32) | utc.dwLowDateTime);
    fflush(stdout);

    for (ULONG iteration = 0; iteration < Iterations; iteration++) {
        HANDLE sock = NULL;
        XSK_UMEM_REG umem = {0};
        UINT32 ringSize = 8;
        XDP_HOOK_ID rxHook = {XDP_HOOK_L2, XDP_HOOK_RX, XDP_HOOK_INSPECT};
        XDP_HOOK_ID txHook = {XDP_HOOK_L2, XDP_HOOK_TX, XDP_HOOK_INJECT};
        HRESULT result = S_OK;

        umem.TotalSize = 8 * 4096;
        umem.ChunkSize = 4096;
        for (PROBE_STAGE stage = StageCreate; stage < StageCount; stage++) {
            LARGE_INTEGER start, end;
            SAMPLE *sample;

            if (stage > StageUmem && Case->Flags == 0) { break; }
            if ((stage == StageRxHook || stage == StageRxRing || stage == StageFillRing) &&
                !(Case->Flags & XSK_BIND_FLAG_RX)) { continue; }
            if ((stage == StageTxHook || stage == StageTxRing || stage == StageCompletionRing) &&
                !(Case->Flags & XSK_BIND_FLAG_TX)) { continue; }
            if (stage == StageActivate && !Case->Activate) { break; }

            QueryPerformanceCounter(&start);
            switch (stage) {
            case StageCreate:
                result = XskCreate(&sock);
                break;
            case StageAllocate:
                umem.Address = VirtualAlloc(NULL, (SIZE_T)umem.TotalSize,
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                result = umem.Address != NULL ? S_OK : HRESULT_FROM_WIN32(GetLastError());
                break;
            case StageUmem:
                result = XskSetSockopt(sock, XSK_SOCKOPT_UMEM_REG, &umem, sizeof(umem));
                break;
            case StageRxHook:
                result = XskSetSockopt(sock, XSK_SOCKOPT_RX_HOOK_ID, &rxHook, sizeof(rxHook));
                break;
            case StageTxHook:
                result = XskSetSockopt(sock, XSK_SOCKOPT_TX_HOOK_ID, &txHook, sizeof(txHook));
                break;
            case StageRxRing:
                result = XskSetSockopt(sock, XSK_SOCKOPT_RX_RING_SIZE, &ringSize, sizeof(ringSize));
                break;
            case StageFillRing:
                result = XskSetSockopt(sock, XSK_SOCKOPT_RX_FILL_RING_SIZE, &ringSize, sizeof(ringSize));
                break;
            case StageTxRing:
                result = XskSetSockopt(sock, XSK_SOCKOPT_TX_RING_SIZE, &ringSize, sizeof(ringSize));
                break;
            case StageCompletionRing:
                result = XskSetSockopt(sock, XSK_SOCKOPT_TX_COMPLETION_RING_SIZE, &ringSize, sizeof(ringSize));
                break;
            case StageBind:
                result = XskBind(sock, IfIndex, QueueId, Case->Flags | XSK_BIND_FLAG_GENERIC);
                break;
            case StageActivate:
                result = XskActivate(sock, 0);
                break;
            default:
                result = E_UNEXPECTED;
                break;
            }
            QueryPerformanceCounter(&end);
            sample = &samples[(SIZE_T)stage * Iterations + counts[stage]++];
            sample->Result = result;
            sample->Microseconds = (end.QuadPart - start.QuadPart) * 1000000.0 / frequency.QuadPart;
            if (FAILED(result)) {
                failures[stage]++;
                if (stage >= StageBind && failures[stage] <= 8) {
                    printf("xdpfaultrate: case=%s attempt=%lu stage=%s start-qpc=%lld hr=0x%08lx latency-us=%.3f\n",
                        Case->Name, iteration, stageNames[stage], start.QuadPart,
                        (ULONG)result, sample->Microseconds);
                }
                break;
            }
        }

        if (SUCCEEDED(result)) {
            complete++;
            bucketSuccess++;
        }
        bucketAttempts++;
        if (sock != NULL && !CloseHandle(sock)) {
            fprintf(stderr, "CloseHandle failed: %lu\n", GetLastError());
            valid = FALSE;
            break;
        }
        if (umem.Address != NULL && !VirtualFree(umem.Address, 0, MEM_RELEASE)) {
            fprintf(stderr, "VirtualFree failed: %lu\n", GetLastError());
            valid = FALSE;
            break;
        }
        QueryPerformanceCounter(&now);
        if (now.QuadPart - bucketStart.QuadPart >= 5 * frequency.QuadPart || iteration + 1 == Iterations) {
            printf("xdpfaultrate: case=%s bucket-end-s=%.3f duration-s=%.3f attempts=%lu full-success=%lu\n",
                Case->Name, (now.QuadPart - caseStart.QuadPart) / (double)frequency.QuadPart,
                (now.QuadPart - bucketStart.QuadPart) / (double)frequency.QuadPart,
                bucketAttempts, bucketSuccess);
            fflush(stdout);
            bucketStart = now;
            bucketAttempts = bucketSuccess = 0;
        }
    }

    for (PROBE_STAGE stage = StageCreate; stage < StageCount; stage++) {
        SAMPLE *stageSamples = &samples[(SIZE_T)stage * Iterations];
        ULONG count = counts[stage];
        ULONG successes = 0;
        if (count == 0) { continue; }
        qsort(stageSamples, count, sizeof(*stageSamples), CompareResult);
        for (ULONG first = 0; first < count;) {
            ULONG next = first + 1;
            while (next < count && stageSamples[next].Result == stageSamples[first].Result) { next++; }
            if (SUCCEEDED(stageSamples[first].Result)) { successes += next - first; }
            printf("xdpfaultrate: case=%s stage=%s hr=0x%08lx count=%lu\n",
                Case->Name, stageNames[stage], (ULONG)stageSamples[first].Result, next - first);
            qsort(stageSamples + first, next - first, sizeof(*stageSamples), CompareLatency);
            printf("xdpfaultrate: case=%s stage=%s hr=0x%08lx latency-us p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
                Case->Name, stageNames[stage], (ULONG)stageSamples[first].Result,
                stageSamples[first + (next - first - 1) * 50 / 100].Microseconds,
                stageSamples[first + (next - first - 1) * 95 / 100].Microseconds,
                stageSamples[first + (next - first - 1) * 99 / 100].Microseconds,
                stageSamples[next - 1].Microseconds);
            first = next;
        }
        printf("xdpfaultrate: case=%s stage=%s success=%lu/%lu (%.2f%%) not-called=%lu\n",
            Case->Name, stageNames[stage], successes, count, successes * 100.0 / count, Iterations - count);
    }
    QueryPerformanceCounter(&now);
    printf("xdpfaultrate: case=%s full=%lu/%lu (%.2f%%) elapsed-s=%.3f valid=%u\n",
        Case->Name, complete, counts[StageCreate], complete * 100.0 / counts[StageCreate],
        (now.QuadPart - caseStart.QuadPart) / (double)frequency.QuadPart, valid);
    free(samples);
    return valid && complete > 0;
}

int
__cdecl
main(
    int argc,
    char **argv
    )
{
    ULONG iterations = 300;
    ULONG ifIndex = 0;
    ULONG queueId = 0;
    BOOLEAN valid = TRUE;

    for (int argument = 1; argument < argc; argument += 2) {
        char *end;
        ULONG value;
        if (argument + 1 >= argc || argv[argument + 1][0] == '-') { goto Usage; }
        errno = 0;
        value = strtoul(argv[argument + 1], &end, 10);
        if (errno != 0 || *end != '\0' || end == argv[argument + 1]) { goto Usage; }
        if (_stricmp(argv[argument], "-Iterations") == 0) { iterations = value; }
        else if (_stricmp(argv[argument], "-IfIndex") == 0) { ifIndex = value; }
        else if (_stricmp(argv[argument], "-QueueId") == 0) { queueId = value; }
        else { goto Usage; }
    }
    if (iterations == 0 || iterations > 100000 || ifIndex == 0) { goto Usage; }
    for (ULONG caseIndex = 0; caseIndex < ARRAYSIZE(cases); caseIndex++) {
        if (!RunCase(&cases[caseIndex], iterations, ifIndex, queueId)) { valid = FALSE; }
    }
    return valid ? 0 : 1;

Usage:
    fprintf(stderr, "Usage: xdpfaultrate -IfIndex <index> [-Iterations <1..100000>] [-QueueId <id>]\n");
    return 2;
}
