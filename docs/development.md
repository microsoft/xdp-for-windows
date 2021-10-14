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
verifier.exe /standard /driver xdp.sys xdpfnmp.sys
shutdown.exe /r /f /t 0
```

Running the tests:

```Powershell
.\tools\setup.ps1 -Install xdp -Verbose
.\tools\setup.ps1 -Install xdpfnmp -Verbose
vstest.console.exe artifacts\bin\x64_Debug\xdpfunctionaltests.dll
.\tools\setup.ps1 -Uninstall xdpfnmp -Verbose
.\tools\setup.ps1 -Uninstall xdp -Verbose
```

Querying the list of test cases:

```Powershell
vstest.console.exe artifacts\bin\x64_Debug\xdpfunctionaltests.dll /lt
```

Running a specific test case:

```Powershell
vstest.console.exe artifacts\bin\x64_Debug\xdpfunctionaltests.dll /TestCaseFilter:"Name=GenericBinding"
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
# 0   - Delay (in minutes) after boot until simulation engages
# WARNING: spinxsk.dll may fail to load xdp.sys due to low resources simulation.
#          Simply re-run spinxsk.dll to try again.
verifier.exe /standard /faults 599 `"`" `"`" 0  /driver fndis.sys xdp.sys xdpmp.sys
shutdown.exe /r /f /t 0
```

Running the test:

```Powershell
.\tools\setup.ps1 -Install fndis -Verbose
.\tools\setup.ps1 -Install xdp -Verbose
.\tools\setup.ps1 -Install xdpmp -Verbose
.\tools\spinxsk.ps1 -QueueCount 2 -Minutes 10 -Stats -Verbose
.\tools\setup.ps1 -Uninstall xdpmp -Verbose
.\tools\setup.ps1 -Uninstall xdp -Verbose
.\tools\setup.ps1 -Uninstall fndis -Verbose
```

## Configuration

### XDPMP poll-mode provider

NDIS:
```Powershell
Set-NetAdapterAdvancedProperty -Name XDPMP -RegistryKeyword PollProvider -DisplayValue NDIS
```

FNDIS:
```Powershell
Set-NetAdapterAdvancedProperty -Name XDPMP -RegistryKeyword PollProvider -DisplayValue FNDIS
```
