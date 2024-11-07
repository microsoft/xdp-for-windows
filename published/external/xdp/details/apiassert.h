//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#ifndef XDP_DETAILS_APIASSERT_H
#define XDP_DETAILS_APIASSERT_H

#ifndef XDPAPI_ASSERT

#ifdef XDPAPI_ASSERT_INTERNAL

//
// Use XDP internal asserts.
//
#include <xdp/details/assert.h>
#define XDPAPI_ASSERT XDP_ASSERT

#else // XDPAPI_ASSERT_INTERNAL

//
// The application did not specify an API assert behavior, so
// ignore asserts.
//
#define XDPAPI_ASSERT(x)

#endif // XDPAPI_ASSERT_INTERNAL

#endif // XDPAPI_ASSERT

#endif
