//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#if !defined(_KERNEL_MODE)
#error "Incorrectly including Kernel Platform Header"
#endif

#define CXPLAT_VIRTUAL_ALLOC(Size, TypeFlags, ProtectionFlags, Tag) \
    CXPLAT_ALLOC_NONPAGED(Size, Tag)
#define CXPLAT_VIRTUAL_FREE(Mem, Size, Type, Tag) CXPLAT_FREE(Mem, Tag);

inline
BOOLEAN
CxPlatSetThreadNodeAffinity(
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
CxPlatSetThreadGroupAffinity(
    LONG GroupNumber,
    DWORD_PTR CpuAffinity
    )
{
    GROUP_AFFINITY group = {0};

    group.Mask = CpuAffinity;
    group.Group = (USHORT)GroupNumber;

    KeSetSystemGroupAffinityThread(&group, NULL);

    return TRUE;
}
