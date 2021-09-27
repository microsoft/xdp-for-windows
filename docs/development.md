# How to develop WinXDP

[![Build Status](https://mscodehub.visualstudio.com/WindowsXDP/_apis/build/status/CI?branchName=main)](https://mscodehub.visualstudio.com/WindowsXDP/_build/latest?definitionId=1746&branchName=main)

## Prerequisites

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
