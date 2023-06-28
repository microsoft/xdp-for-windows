# How to use XDP for Windows

## Prerequisites

- Windows Server 2019 or 2022

## Installation

XDP for Windows consists of a usermode library (xdpapi.dll) and a driver (xdp.sys).

If xdp.sys is not production-signed:

```bat
CertUtil.exe -addstore Root CoreNetSignRoot.cer
CertUtil.exe -addstore TrustedPublisher CoreNetSignRoot.cer
bcdedit.exe /set testsigning on
[reboot]
```

Install:

```bat
msiexec /i xdp-for-windows.msi /quiet
```

Uninstall:

```bat
msiexec /x xdp-for-windows.msi /quiet
```

### Version Upgrade

To upgrade versions of XDP, uninstall the old version and install the new version. If processes have XDP handles open (e.g. sockets, programs) those handles need to be closed for uninstallation to complete.

## Logging

XDP has detailed logging (via WPP) on its cold code paths and lightweight
logging (via manifest-based ETW) on its hot code paths.

### Using log.ps1

The simplest way to capture and view XDP logs is to use the `log.ps1` script.
You'll need to copy the `tools` directory from this repo onto the target system.
All logging instructions require administrator privileges.

To start XDP logging:

```PowerShell
.\tools\log.ps1 -Start
```

To stop logging and convert the trace to plain text, use the following command.
This will create a binary ETL file and a plain text file under `artifacts\logs`.
To successfully convert WPP traces to plain text, the `-SymbolPath` to a directory
containing XDP symbols (.pdb files) must be provided.

```PowerShell
.\tools\log.ps1 -Stop -Convert -SymbolPath Path\To\Symbols
```

The above command can be split into separate `-Stop` and `-Convert` actions when
the plain text file is not needed, or if it is more convenient to convert to
plain text on another system.

### Advanced ETW

These logs can be captured and formatted using any Windows ETW tool. The XDP
project itself uses [Windows Performance
Recorder](https://docs.microsoft.com/en-us/windows-hardware/test/wpt/windows-performance-recorder)
to configure ETW logging, so all XDP providers are included in
[xdptrace.wprp](..\tools\xdptrace.wprp) along with a variety of
scenario-specific profiles.

| Type | GUID                                   |
|------|----------------------------------------|
| ETW  | `580BBDEA-B364-4369-B291-D3539E35D20B` |
| WPP  | `D6143B5C-9FD6-44BA-BA02-FAD9EA0C263D` |

### In-flight recorder

There is also a continuously running WPP logging session writing to an in-kernel
circular buffer; the most recent log entries can be viewed at any time,
including in crash dumps, using the kernel debugger.

```
!rcdrkd.rcdrlogdump xdp
```

### Installer logging

To collect XDP installer traces, append `/l*v filename.log` to the MSI command line.

## Configuration

XDP is in a passive state upon installation. XDP can be configured via a set of
usermode APIs exported from `xdpapi.dll`.

### XDP Queues

The number of XDP queues is determined by the number of RSS queues configured on
a network interface. The XDP queue IDs are assigned [0, N-1] for an interface
with N configured RSS queues. XDP programs and AF_XDP applications bind to RSS
queues using this queue ID space.

### XDP access control

Access to XDP is restricted to `SYSTEM` and the built-in administrators group by default. The `xdpcfg.exe` tool can be used to add or remove privileges. For example, to grant access to `SYSTEM`, built-in administrators, and the user or group represented by the `S-1-5-21-1626206346-3338949459-3778528156-1001` SID:

```PowerShell
xdpcfg.exe SetDeviceSddl "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;S-1-5-21-1626206346-3338949459-3778528156-1001)"
```

The XDP driver must be restarted for these changes to take effect; the configuration is persistent across driver and machine restarts.

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
