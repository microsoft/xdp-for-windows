#pragma once

#ifdef _KERNEL_MODE
#include <wdm.h>
#endif

typedef struct _XDP_FILE_DISPATCH XDP_FILE_DISPATCH;

//
// Type of XDP object to create or open.
//
typedef enum _XDP_OBJECT_TYPE {
    XDP_OBJECT_TYPE_PROGRAM,
    XDP_OBJECT_TYPE_XSK,
    XDP_OBJECT_TYPE_INTERFACE,
} XDP_OBJECT_TYPE;

typedef struct _XDP_FILE_OBJECT_HEADER {
    XDP_OBJECT_TYPE ObjectType;
    XDP_FILE_DISPATCH *Dispatch;
#ifdef _KERNEL_MODE
    PEX_RUNDOWN_REF_CACHE_AWARE RundownRef;
#endif
} XDP_FILE_OBJECT_HEADER;
