//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define XSKBENCHDRV_DEVICE_NAME L"\\Device\\xskbenchdrv"

#define IOCTL_START_SESSION \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_INTERRUPT_SESSION \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// typedef struct SESSION_ARGS {
//     //
//     // Number of arguments.
//     //
//     UINT32 Argc;

//     //
//     // Offset to the beginning of the array of offsets for each arg.
//     // Offset = bytes from the beginning of SESSION_ARGS.
//     //
//     UINT64 ArgvOffsetsArrayOffset;

//     //
//     // Array of offsets for each arg.
//     // Offset = bytes from the beginning of SESSION_ARGS.
//     //
//     // UINT64 ArgvOffsets[Argc];
// } _SESSION_ARGS;

// // +
// // | Argc   | ArgvOff..  |  offset for A  |  offset for B  | Arg 1  | Arg 2
// // +
