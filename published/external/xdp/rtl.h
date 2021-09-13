//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#ifndef XDP_RTL_H
#define XDP_RTL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RTL_PTR_ADD
#define RTL_PTR_ADD(Pointer, Value) \
    ((VOID *)((ULONG_PTR)(Pointer) + (ULONG_PTR)(Value)))
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
