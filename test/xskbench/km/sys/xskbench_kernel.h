//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define printf(format, ...) \
    PlatPrint(__FUNCTION__, __LINE__, 4, format, __VA_ARGS__)

#define printf_error(format, ...) \
    PlatPrint(__FUNCTION__, __LINE__, 3, format, __VA_ARGS__)

#define printf_verbose(format, ...) \
    if (verbose) PlatPrint(__FUNCTION__, __LINE__, 5, format, __VA_ARGS__)

#define ABORT(...) \
    printf_error(__VA_ARGS__); KeBugCheck(0xeeeeeeee)

#define ASSERT_FRE(expr) \
    if (!(expr)) { ABORT("(%s) failed line %d\n", #expr, __LINE__);}

#if DBG
#define VERIFY(expr) NT_ASSERT(expr)
#else
#define VERIFY(expr) (expr)
#endif

#define calloc(a, b) ExAllocatePoolZero(NonPagedPoolNx, (a) * (b), 'vDbX')

#define malloc(a) ExAllocatePoolZero(NonPagedPoolNx, a, 'vDbX')

#define free(a) ExFreePoolWithTag(a, 'vDbX')

#define INFINITE ULONG_MAX

typedef VOID (*PLAT_THREAD_FN) (VOID *Context);

typedef struct _PLAT_THREAD {
    HANDLE Thread;
    PLAT_THREAD_FN Function;
    VOID *Context;
} PLAT_THREAD;

// DWORD
// WINAPI
// PlatThreadFunctionWrapper(
//     _In_ LPVOID lpParameter
//     );

// #ifdef DEFINE_PLAT_THREAD_FUNCTION_WRAPPER
// inline
// DWORD
// WINAPI
// PlatThreadFunctionWrapper(
//     _In_ LPVOID lpParameter
//     )
// {
//     PLAT_THREAD *Thread = (PLAT_THREAD *)lpParameter;
//     Thread->Function(Thread->Context);
//     return 0;
// }
// #endif

inline
BOOLEAN
PlatCreateThread(PLAT_THREAD *P, PLAT_THREAD_FN F, VOID *C)
{
    P->Function = F;
    P->Context = C;
    NTSTATUS Status =
        PsCreateSystemThread(&P->Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, F, C);
    return Status == STATUS_SUCCESS;
}

inline
VOID
PlatWaitThread(PLAT_THREAD *P, ULONG TimeoutMs)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    VOID *ThreadObject;

    RtlZeroMemory(&Timeout, sizeof(Timeout));
    if (TimeoutMs != INFINITE) {
        Timeout.QuadPart = -1 * (ULONGLONG)TimeoutMs * 10000;
    }

    Status = ObReferenceObjectByHandle(P->Thread, THREAD_ALL_ACCESS, NULL, KernelMode, &ThreadObject, NULL);
    ASSERT(Status == STATUS_SUCCESS);

    KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, (TimeoutMs == INFINITE) ? NULL : &Timeout);

    ObDereferenceObject(ThreadObject);
}

typedef struct _PLAT_EVENT {
    KEVENT Event;
} PLAT_EVENT;

inline
VOID
PlatInitializeEvent(PLAT_EVENT *P)
{
    KeInitializeEvent(&P->Event, NotificationEvent, FALSE);
}

inline
VOID
PlatSetEvent(PLAT_EVENT *P)
{
    KeSetEvent(&P->Event, 0, FALSE);
}

inline
VOID
PlatWaitEvent(PLAT_EVENT *P, ULONG TimeoutMs)
{
    LARGE_INTEGER Timeout;
    RtlZeroMemory(&Timeout, sizeof(Timeout));
    if (TimeoutMs != INFINITE) {
        Timeout.QuadPart = -1 * (ULONGLONG)TimeoutMs * 10000;
    }
    KeWaitForSingleObject(&P->Event, Executive, KernelMode, FALSE, (TimeoutMs == INFINITE) ? NULL : &Timeout);
}

inline
ULONGLONG
PlatGetElapsedMsSinceBoot()
{
    LARGE_INTEGER TickCount;
    KeQueryTickCount(&TickCount);
    return KeQueryTimeIncrement() * TickCount.QuadPart / 10000;
}

inline
BOOLEAN
PlatQueryPerformanceCounter(
    LARGE_INTEGER *Counter
    )
{
    *Counter = KeQueryPerformanceCounter(NULL);
    return TRUE;
}

inline
BOOLEAN
PlatQueryPerformanceFrequency(
    LARGE_INTEGER *Frequency
    )
{
    KeQueryPerformanceCounter(Frequency);
    return TRUE;
}

inline
BOOLEAN
PlatSetThreadNodeAffinity(
    LONG NodeAffinity
    )
{
    GROUP_AFFINITY group;

    KeQueryNodeActiveAffinity((USHORT)NodeAffinity, &group, NULL);
    KeSetSystemGroupAffinityThread(&group, NULL);

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

    KeSetSystemGroupAffinityThread(&group, NULL);

    return TRUE;
}

VOID *
PlatGetXdpApiProviderBindingContext(
    VOID
    );

VOID
PlatInitializeXdpApi(
    VOID
    );

VOID
PlatUninitializeXdpApi(
    VOID
    );

VOID
PlatPrint(
    char* Function,
    int Line,
    char level,
    const char* format,
    ...
    );
