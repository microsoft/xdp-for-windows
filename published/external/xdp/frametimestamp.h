//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820) // warn if the compiler inserted padding
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union
#pragma warning(disable:4214) // nonstandard extension used: bit field types other than int

typedef struct _XDP_FRAME_TIMESTAMP {
    UINT64 Timestamp;
} XDP_FRAME_TIMESTAMP;

C_ASSERT(sizeof(XDP_FRAME_TIMESTAMP) == 8);

#pragma warning(pop)

EXTERN_C_END
