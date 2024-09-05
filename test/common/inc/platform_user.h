//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#ifdef _KERNEL_MODE
#error "Incorrectly including User Platform Header"
#endif

#define CXPLAT_VIRTUAL_ALLOC(Size, TypeFlags, ProtectionFlags, Tag) \
    VirtualAlloc(NULL, Size, TypeFlags, ProtectionFlags)
#define CXPLAT_VIRTUAL_FREE(Mem, Size, Type, Tag) VirtualFree(Mem, Size, Type);

inline
BOOLEAN
CxPlatSetThreadNodeAffinity(
    LONG NodeAffinity
    )
{
    GROUP_AFFINITY group;

    if (!GetNumaNodeProcessorMaskEx((USHORT)NodeAffinity, &group)) {
        CXPLAT_DBG_ASSERT(FALSE);
        return FALSE;
    }
    if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
        CXPLAT_DBG_ASSERT(FALSE);
        return FALSE;
    }

    return TRUE;
}

inline
BOOLEAN
CxPlatSetThreadGroupAffinity(
    LONG GroupNumber,
    DWORD_PTR CpuAffinity
    )
{
    GROUP_AFFINITY group = {0};

    group.Mask = CpuAffinity;
    group.Group = (WORD)GroupNumber;
    if (!SetThreadGroupAffinity(GetCurrentThread(), &group, NULL)) {
        CXPLAT_DBG_ASSERT(FALSE);
        return FALSE;
    }

    return TRUE;
}
