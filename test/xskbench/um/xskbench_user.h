//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define printf_error(...) \
    fprintf(stderr, __VA_ARGS__)

#define printf_verbose(format, ...) \
    if (verbose) { LARGE_INTEGER Qpc; QueryPerformanceCounter(&Qpc); printf("Qpc=%llu " format, Qpc.QuadPart, __VA_ARGS__); }

#define ABORT(...) \
    printf_error(__VA_ARGS__); exit(1)

#define ASSERT_FRE(expr) \
    if (!(expr)) { ABORT("(%s) failed line %d\n", #expr, __LINE__);}

#define ASSERT(expr) \
    assert(expr)

#if DBG
#define VERIFY(expr) ASSERT(expr)
#else
#define VERIFY(expr) (expr)
#endif

#define ALIGN_DOWN_BY(length, alignment) \
    ((ULONG_PTR)(length)& ~(alignment - 1))
#define ALIGN_UP_BY(length, alignment) \
    (ALIGN_DOWN_BY(((ULONG_PTR)(length)+alignment - 1), alignment))

typedef VOID (*PLAT_THREAD_FN) (VOID *Context);

typedef struct _PLAT_THREAD {
    HANDLE Thread;
    PLAT_THREAD_FN Function;
    VOID *Context;
} PLAT_THREAD;

DWORD
WINAPI
PlatThreadFunctionWrapper(
    _In_ LPVOID lpParameter
    );

#ifdef DEFINE_PLAT_THREAD_FUNCTION_WRAPPER
inline
DWORD
WINAPI
PlatThreadFunctionWrapper(
    _In_ LPVOID lpParameter
    )
{
    PLAT_THREAD *Thread = (PLAT_THREAD *)lpParameter;
    Thread->Function(Thread->Context);
    return 0;
}
#endif

inline
BOOLEAN
PlatCreateThread(PLAT_THREAD *P, PLAT_THREAD_FN F, VOID *C)
{
    P->Function = F;
    P->Context = C;
    P->Thread = CreateThread(NULL, 0, PlatThreadFunctionWrapper, P, 0, NULL);
    return P->Thread != NULL;
}

inline
VOID
PlatWaitThread(PLAT_THREAD *P, ULONG TimeoutMs)
{
    WaitForSingleObject(P->Thread, TimeoutMs);
}

typedef struct _PLAT_EVENT {
    HANDLE Event;
} PLAT_EVENT;

inline
VOID
PlatInitializeEvent(PLAT_EVENT *P)
{
    P->Event = CreateEvent(NULL, FALSE, FALSE, NULL);
    ASSERT_FRE(P->Event != NULL);
}

inline
VOID
PlatSetEvent(PLAT_EVENT *P)
{
    SetEvent(P->Event);
}

inline
VOID
PlatWaitEvent(PLAT_EVENT *P, ULONG TimeoutMs)
{
    WaitForSingleObject(P->Event, TimeoutMs);
}

inline
ULONGLONG
PlatGetElapsedMsSinceBoot()
{
    return GetTickCount64();
}

inline
BOOLEAN
PlatQueryPerformanceCounter(
    LARGE_INTEGER *Counter
    )
{
    return !!QueryPerformanceCounter(Counter);
}

inline
BOOLEAN
PlatQueryPerformanceFrequency(
    LARGE_INTEGER *Frequency
    )
{
    return !!QueryPerformanceFrequency(Frequency);
}

inline
BOOLEAN
PlatSetThreadNodeAffinity(
    LONG NodeAffinity
    )
{
    GROUP_AFFINITY group;

    if (!GetNumaNodeProcessorMaskEx((USHORT)NodeAffinity, &group)) {
        ASSERT(FALSE);
        return FALSE;
    }
    if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
        ASSERT(FALSE);
        return FALSE;
    }

    return TRUE;
}

inline
BOOLEAN
PlatSetThreadGroupAffinity(
    LONG GroupNumber,
    DWORD_PTR CpuAffinity
    )
{
    GROUP_AFFINITY group = {0};

    group.Mask = CpuAffinity;
    group.Group = (WORD)GroupNumber;
    if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
        ASSERT(FALSE);
        return FALSE;
    }

    return TRUE;
}

inline
BOOLEAN
PlatSetThreadIdealProcessor(
    LONG IdealCpu
    )
{
    DWORD oldCpu;
    oldCpu = SetThreadIdealProcessor(GetCurrentThread(), IdealCpu);
    ASSERT(oldCpu != -1);
    if (oldCpu == -1) {
        return FALSE;
    }

    return TRUE;
}

VOID
PlatInitializeXdpApi(
    VOID
    );

VOID
PlatUninitializeXdpApi(
    VOID
    );
