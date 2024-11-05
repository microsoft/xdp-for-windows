//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

HRESULT
XskCreateV2(
    _Out_ HANDLE* Socket
    )
{
    return _XskCreateVersion(Socket, XDP_API_VERSION_2);
}
