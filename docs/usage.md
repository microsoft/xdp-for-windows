# How to use XDP for Windows

## Prerequisites

- Windows Server 2019 or 2022

## Installation

XDP for Windows consists of a driver (xdp.sys) and header-only user mode APIs. For backward compatibility with older applications, a user mode library (xdpapi.dll) is also provided, but its use is not recommended for new applications.

### Install the Latest (1.x) Official

```PowerShell
Invoke-WebRequest https://aka.ms/xdp-v1.msi -OutFile xdp.msi
msiexec.exe /i xdp.msi /quiet
```

Optionally, the runtime may be installed from a [`XDP-for-Windows-Runtime.<arch>`](https://www.nuget.org/packages?q=xdp-for-windows.runtime) nuget package. The package must first be restored or its contents otherwise extracted, and then the following command installs the base `xdp` runtime components:

```Powershell
xdp-setup.ps1 -Install xdp
```

### Install a Test Version

If xdp.sys is not an official production-signed release, its test sigining certificate must be installed and test signing must be enabled before installing XDP. Secure boot must be [disabled](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/disabling-secure-boot) before test signing can be enabled.

Here's an example set of commands to extract the test signing certificate from an MSI, install it as a trusted certificate, and enable test signing:

```PowerShell
$CertFileName = 'xdp.cer'
Get-AuthenticodeSignature 'xdp-for-windows.msi' | Select-Object -ExpandProperty SignerCertificate | Export-Certificate -Type CERT -FilePath $CertFileName
Import-Certificate -FilePath $CertFileName -CertStoreLocation 'cert:\localmachine\root'
Import-Certificate -FilePath $CertFileName -CertStoreLocation 'cert:\localmachine\trustedpublisher'
bcdedit.exe /set testsigning on
[reboot]
```

Install:

```bat
msiexec.exe /i xdp-for-windows.msi /quiet
```

Uninstall:

```bat
msiexec.exe /x xdp-for-windows.msi /quiet
```

### Enable eBPF Support

**Note** eBPF support is experimental and is not officially supported by XDP.

Starting with XDP version 1.1, experimental eBPF support can be enabled by appending an `ADDLOCAL=xdp_ebpf` parameter to the `msiexec.exe` install commands.

When using the runtime nuget package instead of the runtime MSI, run the following command after [installing eBPF-for-Windows](https://github.com/microsoft/ebpf-for-windows/blob/main/docs/InstallEbpf.md) and the base `xdp` component:

```Powershell
xdp-setup.ps1 -Install xdpebpf
```

The eBPF headers for XDP are available in the XDP SDK NuGet package:
- `xdp/ebpfhook.h` - XDP eBPF program types and context structures
- `xdp/bpf_helpers.h` - BPF helper functions and map definitions
- `xdp/bpf_endian.h` - Byte order conversion macros
- `xdp/net/if_ether.h` - Ethernet protocol definitions
- `xdp/net/ip.h` - IP protocol definitions

For general eBPF usage documentation, see [eBPF Getting Started](https://github.com/microsoft/ebpf-for-windows/blob/main/docs/GettingStarted.md#using-ebpf-in-development).

```Powershell
xdp-setup.ps1 -Install xdpebpfexport
```

Developers will also need to execute `xdpbpfexport.exe` prior to verifying and compiling XDP eBPF programs; the binary is included in XDP developer NuGet packages. This tool populates eBPF registry keys with information about the XDP program type and helper functions. 

eBPF-for-Windows will look in the HKCU (Current User) registry store first, and if the required values are present, it will use those configurations. If the values are not present in HKCU, then it will fall back to looking in the HKLM (Local Machine) registry.

For development scenarios, configuring the registry store in just the HKCU location is usually sufficient. For runtime usage, the registry store configuration will depend on how eBPF-for-Windows is installed and how its registry store is configured. Using the eBPF-for-Windows MSI installer will install registry entries into both HKCU and HKLM stores, which is the same approach used by the XDP MSI.

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
user mode APIs provided in XDP headers. These APIs are header-only implementations (using inline functions) that issue IOCTLs to the XDP driver.

**Note:** For backward compatibility, older applications using `XDP_API_VERSION_1` or `XDP_API_VERSION_2` may use the deprecated `xdpapi.dll` library. New applications should use `XDP_API_VERSION_3` or later, which provides all APIs as header-only implementations.

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
- xdpapi.h (XDP API - header-only implementation for `XDP_API_VERSION_3` or later)
- afxdp_helper.h (optional AF_XDP helpers)

These headers provide inline function implementations that interact directly with the XDP driver via IOCTLs, eliminating the need for a separate DLL.

## Generic XDP

A generic XDP implementation is provided by the XDP driver. Generic XDP inspects
the NBL data path of any NDIS interface without requiring third party driver
changes.

## Native XDP

Native XDP requires an updated NDIS driver.
