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
