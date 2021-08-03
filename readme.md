# XDP Developer Readme

## Installing XDP

The XDP runtime consists of a kernel mode driver and a user mode library.

To install the XDP user mode libary, add the root XDP directory to the `PATH`
environment variable or copy msxdp.dll into `system32`.

To install the XDP driver:

```PowerShell
certmgr.exe -add .\test\testroot.cer -s root
bcdedit.exe /set testsigning on
netcfg.exe -l .\xdp.inf -c s -i ms_xdp
```

To uninstall the XDP driver:

```PowerShell
netcfg.exe -u ms_xdp
pnputil.exe /delete-driver xdp.inf
```

## XDP Configuration

XDP is in a passive state upon installation. XDP can be configured via a set of
usermode APIs exported from `msxdp.dll`.

### XDP Queues

The number of XDP queues is determined by the number of RSS queues configured on
a network interface. The XDP queue IDs are assigned [0, N-1] for an interface
with N configured RSS queues. XDP programs and AF_XDP applications bind to RSS
queues using this queue ID space.

See the `xskbench` application for example usage.

## AF_XDP

XDP allows traffic to be redirected to a user space application using the AF_XDP
API.

The top level headers required by AF_XDP applications are:

- afxdp.h (AF_XDP sockets API)
- msxdp.h (XDP program API)
- afxdp_helper.h (optional AF_XDP helpers)

See `xskbench` for example usage.

## Generic XDP

A generic XDP implementation is provided by the XDP driver. Generic XDP inspects
the NBL data path of any NDIS interface without requiring third party driver
changes.

## Native XDP

Native XDP requires an updated NDIS driver.
