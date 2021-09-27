# How to use WinXdp

## Installation

WinXdp consists of a usermode library (msxdp.dll) and a driver (xdp.sys).

To install the library, add the root XDP directory to the `PATH`
environment variable or copy msxdp.dll into `system32`.

To install the driver:

```PowerShell
certmgr.exe -add .\test\testroot.cer -s root
bcdedit.exe /set testsigning on
netcfg.exe -l .\xdp.inf -c s -i ms_xdp
```

To uninstall the driver:

```PowerShell
netcfg.exe -u ms_xdp
pnputil.exe /delete-driver xdp.inf
```

## Configuration

XDP is in a passive state upon installation. XDP can be configured via a set of
usermode APIs exported from `msxdp.dll`.

### XDP Queues

The number of XDP queues is determined by the number of RSS queues configured on
a network interface. The XDP queue IDs are assigned [0, N-1] for an interface
with N configured RSS queues. XDP programs and AF_XDP applications bind to RSS
queues using this queue ID space.

See [`xskbench`](test\xskbench\xskbench.c) for example usage.

## AF_XDP

AF_XDP is the API for redirecting traffic to a usermode application. To use the API,
include the following headers:

- afxdp.h (AF_XDP sockets API)
- msxdp.h (XDP program API)
- afxdp_helper.h (optional AF_XDP helpers)

See [`xskbench`](test\xskbench\xskbench.c) for example usage.

## Generic XDP

A generic XDP implementation is provided by the XDP driver. Generic XDP inspects
the NBL data path of any NDIS interface without requiring third party driver
changes.

## Native XDP

Native XDP requires an updated NDIS driver.
