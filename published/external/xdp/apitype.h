#pragma once

#ifndef XDPAPI
#define XDPAPI __declspec(dllimport)
#endif

#if defined(_KERNEL_MODE)
typedef VOID XDP_API_PROVIDER_BINDING_CONTEXT;
#endif // defined(_KERNEL_MODE)
