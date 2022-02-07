# How to develop WinXDP

## Get the code

Clone this repo and ensure all submodules are cloned with the `--recursive` option:

```
git clone https://mscodehub.visualstudio.com/WindowsXDP/_git/xdp --recursive
```

Or, if the repo was already cloned nonrecursively:

```
git submodule update --init --recursive
```

## Build the code

### Prerequisites

- [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
  - Latest Spectre-mitigated libs (via "Individual components" section of Visual Studio Installer)
- [Windows Driver Kit](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
  (LTSC 2022 WDK or newer)

### Building

Run in a Visual Studio "Developer Command Prompt":

```PowerShell
.\tools\build.ps1
```

## Test the code

The test machine must have the "artifacts" and "tools" directories from the repo, either
by cloning the repo and building the code or by copying them from another system. The
file layout is assumed to be identical to that of the repo.

### Running functional tests

One-time setup:

```Powershell
.\tools\prepare-machine.ps1 -ForTest -NoReboot
verifier.exe /standard /driver xdp.sys xdpfnmp.sys xdpfnlwf.sys
shutdown.exe /r /f /t 0
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
.\tools\prepare-machine.ps1 -ForTest -NoReboot
# Verifier configuration: standard flags with low resources simulation.
# 599 - Failure probability (599/10000 = 5.99%)
#       N.B. If left to the default value, roughly every 5 minutes verifier
#       will fail all allocations within a 10 second interval. This behavior
#       complicates the spinxsk socket setup statistics. Setting it to a
#       non-default value disables this behavior.
# ""  - Pool tag filter
# ""  - Application filter
# 1   - Delay (in minutes) after boot until simulation engages
#       This is the lowest value configurable via verifier.exe.
# WARNING: xdp.sys itself may fail to load due to low resources simulation.
verifier.exe /standard /faults 599 `"`" `"`" 1  /driver xdp.sys
shutdown.exe /r /f /t 0
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
