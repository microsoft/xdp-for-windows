# Windows XDP

A Windows interface similar to the [XDP (eXpress Data Path)](https://en.wikipedia.org/wiki/Express_Data_Path)
design. Used to send and receive network packets at extremely high rates, by bypassing most of the
OS networking stack.

[![Build Status](https://mscodehub.visualstudio.com/WindowsXDP/_apis/build/status/CI?branchName=main)](https://mscodehub.visualstudio.com/WindowsXDP/_build/latest?definitionId=1746&branchName=main)

# Getting Started

## Prerequisites

The following need to be installed before the project can be built.

- [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
  - Latest Spectre-mitigated libs (via "Individual components" section of Visual Studio Installer)
- [Windows Driver Kit](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
  (LTSC 2022 WDK or newer)

## Get the code

Clone this repo and ensure all submodules are cloned with the `--recursive`
option. Or run `git submodule update --init --recursive` in an already-cloned
repo.

```
git clone https://mscodehub.visualstudio.com/WindowsXDP/_git/xdp --recursive
```

## Build the code

Open xdp.sln in Visual Studio and press ctrl+shift+B to build the code or you
can run `build.ps1` in the "Developer Command Prompt":

```PowerShell
.\tools\build.ps1
```

## Install XDP

The XDP runtime consists of a kernel mode driver and a user mode library.

To install the XDP user mode library, add the root XDP directory to the `PATH`
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

# Windows XDP Design

## XDP Configuration

XDP is in a passive state upon installation. XDP can be configured via a set of
usermode APIs exported from `msxdp.dll`.

### XDP Queues

The number of XDP queues is determined by the number of RSS queues configured on
a network interface. The XDP queue IDs are assigned [0, N-1] for an interface
with N configured RSS queues. XDP programs and AF_XDP applications bind to RSS
queues using this queue ID space.

See [`xskbench`](test\xskbench\xskbench.c) for example usage.

## AF_XDP

XDP allows traffic to be redirected to a user space application using the AF_XDP
API.

The top level headers required by AF_XDP applications are:

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
