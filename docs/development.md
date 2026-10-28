# How to develop XDP for Windows

## Get the code

Clone this repo and ensure all submodules are cloned with the `--recursive` option:

```
git clone https://github.com/microsoft/xdp-for-windows.git --recursive
```

Or, if the repo was already cloned nonrecursively:

```
git submodule update --init --recursive
```

## Build the code

### Prerequisites

- [Visual Studio](https://visualstudio.microsoft.com/downloads/)
  - Visual Studio 2022 or newer is required.

  during installation, ensure the following packages/components are installed:
    - "Desktop development with C++" (via "Workload")
    - Latest Spectre-mitigated libs (via "Individual components")
    - C++ Address Sanitizer (via "Individual components")

- [Clang LLVM](https://github.com/microsoft/ebpf-for-windows/blob/main/docs/GettingStarted.md#Prerequisites)
  - Install Clang for Windows 64-bit (version 18.1.8). The latest version of clang that ships with the Visual Studio installer does not support bpf as a target.
  - Make sure clang.exe is in your PATH and that `clang --print-targets | Select-String "bpf"` prints a non-empty output.

- [NuGet Windows x86 Commandline](https://www.nuget.org/downloads)
  - Version 6.3.1 or higher is required by the eBPF-for-Windows project.
- [Windows WDK and matching SDK](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
  - Version 10.0.26100.3323 is required to build eBPF programs, including tests. This prerequisite will eventually be [removed](https://github.com/microsoft/ebpf-for-windows/issues/4379) by the eBPF project.
  - The standalone SDK and WDK are not required to build the XDP runtime.

### Building

Run in a Visual Studio "Developer Command Prompt" (non-admin window):

```PowerShell
.\tools\build.ps1
```

## Test the code

The test machine must have the "artifacts" and "tools" directories from the repo, either
by cloning the repo and building the code or by copying them from another system. The
file layout is assumed to be identical to that of the repo.

For a streamlined dev-machine -> test-machine workflow over PowerShell remoting,
see [Remote testing](./remote-testing.md). The same test scripts described
below accept a `-ComputerName` parameter to run against a remote test machine.

### Running functional tests

One-time setup:

```Powershell
.\tools\prepare-machine.ps1 -ForFunctionalTest
```

Running the tests:

```Powershell
.\tools\functional.ps1
```

Querying the list of test cases:

```Powershell
.\tools\functional.ps1 -ListTestCases
```

Running a specific test case:

```Powershell
.\tools\functional.ps1 -TestCaseFilter "Name=GenericBinding"
```

After the test, convert the logs:

```Powershell
.\tools\log.ps1 -Convert -Name xdpfunc
```

### Running spinxsk

One-time setup:

```Powershell
.\tools\prepare-machine.ps1 -ForSpinxskTest
```

Optionally, disable the legacy TDX/TDI driver stack for greater reliability:
```Powershell
#
# Disable TDX and its dependent service NetBT. These drivers are implicated in
# some NDIS control path hangs.
#
reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\netbt /v Start /d 4 /t REG_DWORD /f
reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\tdx /v Start /d 4 /t REG_DWORD /f
shutdown.exe /r /f /t 0
```

Running the test:

```Powershell
.\tools\spinxsk.ps1 -XdpmpPollProvider FNDIS -QueueCount 2 -Minutes 100
```

Or, to run until ctrl+c is pressed:

```Powershell
.\tools\spinxsk.ps1 -XdpmpPollProvider FNDIS -QueueCount 2
```

After the test, convert the logs:

```Powershell
.\tools\log.ps1 -Convert -Name spinxsk
```

### Performance inspection coverage

CI measures both the deprecated built-in rules engine and eBPF inspection:

- RX filter benchmarks run Drop in Generic and Native modes on x64 and arm64.
- XSK benchmarks run RX and FWD in Generic and Native modes, including
  zero-copy simulation, with the `xsk_redirect` eBPF program and an XSKMAP.
- TX-only, TX-inspect/RX-inject, Winsock/RIO, and ring benchmarks retain their
  existing coverage. The eBPF hook currently supports RX inspection only; ring
  benchmarks do not execute an inspection program.

The perf scripts preserve built-in inspection by default. Pass `-Ebpf` to
`rxfilterperf.ps1`, or `-Ebpf -Modes RX,FWD` to `xskperfsuite.ps1`, to select
eBPF. eBPF scenarios and CPU trace names include `-EBPF` so the two engines
remain separate in summaries and comparisons. `-EbpfPreinstalled` preserves
an existing eBPF runtime; otherwise the harness installs and removes it.
Prerelease CI uses a private stack matching the build-time eBPF version.

XSK baseline comparisons include eBPF when the base reference supports the
new benchmark switch. Older base references explicitly skip eBPF and continue
to compare the existing built-in scenarios.

## Configuration

### XDPMP poll-mode provider

NDIS:
```Powershell
Set-NetAdapterAdvancedProperty -Name XDPMP -RegistryKeyword PollProvider -DisplayValue NDIS
Set-NetAdapterDataPathConfiguration -Name XDPMP -Profile Passive
```

FNDIS:
```Powershell
Set-NetAdapterAdvancedProperty -Name XDPMP -RegistryKeyword PollProvider -DisplayValue FNDIS
```
