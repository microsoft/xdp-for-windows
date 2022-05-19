# How to use XDP for Windows

## Installation

XDP for Windows consists of a usermode library (xdpapi.dll) and a driver (xdp.sys).

If xdp.sys is not production-signed:
```PowerShell
CertUtil.exe -addstore Root CoreNetSignRoot.cer
CertUtil.exe -addstore TrustedPublisher CoreNetSignRoot.cer
bcdedit.exe /set testsigning on
[reboot]
```

Install:
```PowerShell
netcfg.exe -l .\xdp.inf -c s -i ms_xdp
```

Uninstall:
```PowerShell
netcfg.exe -u ms_xdp
pnputil.exe /delete-driver xdp.inf
```

## Configuration

XDP is in a passive state upon installation. XDP can be configured via a set of
usermode APIs exported from `xdpapi.dll`.

### XDP Queues

The number of XDP queues is determined by the number of RSS queues configured on
a network interface. The XDP queue IDs are assigned [0, N-1] for an interface
with N configured RSS queues. XDP programs and AF_XDP applications bind to RSS
queues using this queue ID space.

## AF_XDP

AF_XDP is the API for redirecting traffic to a usermode application. To use the API,
include the following headers:

- afxdp.h (AF_XDP sockets API)
- xdpapi.h (XDP API)
- afxdp_helper.h (optional AF_XDP helpers)

## Generic XDP

A generic XDP implementation is provided by the XDP driver. Generic XDP inspects
the NBL data path of any NDIS interface without requiring third party driver
changes.

## Native XDP

Native XDP requires an updated NDIS driver.
