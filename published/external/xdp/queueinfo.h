//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_QUEUE_INFO_H
#define XDP_QUEUE_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _XDP_QUEUE_TYPE {
    XDP_QUEUE_TYPE_DEFAULT_RSS,
} XDP_QUEUE_TYPE;

typedef struct _XDP_QUEUE_INFO {
    XDP_OBJECT_HEADER Header;
    XDP_QUEUE_TYPE QueueType;
    UINT32 QueueId;
} XDP_QUEUE_INFO;

#define XDP_QUEUE_INFO_REVISION_1 1

#define XDP_SIZEOF_QUEUE_INFO_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(XDP_QUEUE_INFO, QueueId)

#ifdef __cplusplus
} // extern "C"
#endif

#endif
