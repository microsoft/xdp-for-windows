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
