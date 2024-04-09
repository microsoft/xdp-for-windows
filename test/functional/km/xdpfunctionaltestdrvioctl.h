//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

//
// Kernel Mode Driver Interface
//

#define FUNCTIONAL_TEST_DRIVER_NAME "xdpfunctionaltestdrv"

#define IoGetFunctionCodeFromCtlCode( ControlCode ) (\
    ( ControlCode >> 2) & 0x00000FFF )

//
// IOCTL Interface
//

#define IOCTL_LOAD_API \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_DATA)

#define MAX_IOCTL_FUNC_CODE 1

EXTERN_C_END
