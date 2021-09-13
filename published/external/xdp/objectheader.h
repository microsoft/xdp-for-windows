//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#ifndef XDP_OBJECT_HEADER_H
#define XDP_OBJECT_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _XDP_OBJECT_HEADER {
    UINT32 Revision;
    UINT32 Size;
} XDP_OBJECT_HEADER;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
