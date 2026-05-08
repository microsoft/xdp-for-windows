# Copilot Instructions for XDP for Windows

## Repository Overview

XDP for Windows is a high-performance Windows packet processing framework inspired by Linux XDP. It enables sending and receiving network packets at high rates by bypassing most of the OS networking stack.

**Key facts:**
- **Languages**: C (kernel drivers), C++ (user-mode tests/samples), PowerShell (build/test scripts)
- **Frameworks**: Windows Driver Kit (WDK), NDIS, TAEF (test framework)
- **Target**: Windows Server 2019/2022 and Prerelease builds, x64 and arm64 platforms
- **Build Tool**: MSBuild via Visual Studio Developer Command Prompt

## Build Instructions

### Prerequisites
- Visual Studio 2022+ with:
  - "Desktop development with C++" workload
  - Latest Spectre-mitigated libs
  - C++ Address Sanitizer
  - C++ Clang Compiler for Windows
- NuGet CLI 6.3.1+
- Git submodules initialized (`git submodule update --init --recursive`)

### Building

**Always run builds from a Visual Studio Developer Command Prompt.**

1. Prepare the machine (downloads dependencies):
   ```powershell
   .\tools\prepare-machine.ps1 -ForBuild
   ```

2. Restore NuGet packages (required before every build if packages changed):
   ```powershell
   msbuild.exe xdp.sln /t:restore /p:RestoreConfigFile=tools/nuget.config /p:Configuration=Debug /p:Platform=x64
   ```

3. Prepare for eBPF compilation (if building eBPF programs):
   ```powershell
   .\tools\prepare-machine.ps1 -ForEbpfBuild
   ```

4. Build the solution:
   ```powershell
   msbuild xdp.sln /m /p:Configuration=Debug /p:Platform=x64 /p:SignMode=TestSign /p:IsAdmin=true /nodeReuse:false
   ```

**Or use the convenience script:**
```powershell
.\tools\build.ps1
```

**Important**: Use `/nodeReuse:false` to avoid cached state issues with WDK's build tasks.

### Build Configurations
- **Config**: `Debug` or `Release`
- **Platform**: `x64` or `arm64`
- **Build artifacts**: `artifacts\bin\{Platform}_{Config}\`

## Testing

All testing must occur on a separate test machine. Do not try to run tests locally.

> [!IMPORTANT]
> **Always pass `-ComputerName <machine>` explicitly on every test
> command.** Do not rely on `Set-XdpRemoteDefault` /
> `remote-set-default.ps1` alone — the session default lives in the
> PowerShell global scope and is silently lost when a new terminal is
> spawned (which happens any time a previous command spawned its own
> shell, e.g. after `Start-Process`, after a kd session, or whenever the
> tool host opens a fresh terminal). A test script invoked without
> `-ComputerName` and without an active session default will run
> **against the dev machine**, which can damage local state.
> The session default is a convenience for the human at the keyboard;
> for AI-driven runs treat `-ComputerName` as required.

### Running tests against a remote test machine (PREFERRED for AI agents)

The repo's test scripts (`tools\functional.ps1`, `tools\spinxsk.ps1`,
`tools\prepare-machine.ps1` for non-build scenarios, `tools\check-drivers.ps1`,
`tools\rxfilter.ps1`, `tools\rxfilterperf.ps1`, `tools\ringperf.ps1`,
`tools\xskmaprx.ps1`, `tools\xskfwdkm.ps1`, `tools\pktfuzz.ps1`) all
support a `-ComputerName` parameter that transparently runs the script on
a remote test machine via PowerShell remoting. See [docs/remote-testing.md](../docs/remote-testing.md).

**Workflow for AI agents:**

1. **Ask the user for the test machine name once per chat session.** Use a
   clarifying question. Example prompt: *"Which test machine should I run
   tests on?"*. Accept hostname, FQDN, or IP. Remember the value and
   pass it as `-ComputerName <machine>` on **every** test-script
   invocation for the rest of the session.

2. **Optionally set the session default for the human's convenience:**
   ```powershell
   .\tools\remote-set-default.ps1 <machine>
   ```
   This is *not* a substitute for `-ComputerName` in agent-driven
   commands (see the IMPORTANT note above). Continue to pass
   `-ComputerName` explicitly.

3. **Run any test script with `-ComputerName`** — it auto-forwards to
   the remote and streams output back:
   ```powershell
   .\tools\functional.ps1 -ComputerName <machine> -ListTestCases
   .\tools\functional.ps1 -ComputerName <machine> -TestCaseFilter "Name=GenericBinding"
   .\tools\spinxsk.ps1 -ComputerName <machine> -Minutes 5
   ```

4. **Credentials.** On first connect the script auto-adds the host to
   WSMan TrustedHosts (UAC prompt) and, if WinRM rejects with auth error,
   prompts at the **console** (`Read-Host`, no GUI) for username and
   password. Credentials are then cached in
   `$global:XdpRemoteCredentialCache` for the rest of the session. Do
   **not** generate `Get-Credential` calls or store passwords in scripts.

5. **One-time setup on the test machine itself** (must be done by the
   user, requires elevation):
   ```powershell
   .\tools\remote.ps1 -EnableRemoting
   ```
   If the user has not done this, the connection will fail with
   "WinRM client cannot process the request" or similar. Tell them to
   run `tools\remote.ps1 -EnableRemoting` on the test machine.

6. **Build first.** Remote scripts deploy `artifacts\bin\<plat>_<cfg>\`
   from the dev machine. Always run `.\tools\build.ps1` (or rely on a
   recent build) before remote test commands.

**Build-only scripts always run locally and ignore `-ComputerName`:**
`build.ps1`, `merge-artifacts.ps1`, `publish-nuget.ps1`,
`prepare-machine.ps1 -ForBuild` / `-ForEbpfBuild`,
`create-test-archive.ps1`, `update-nuspec.ps1`.

### Monitoring the test machine kernel debugger

Test machines typically have a kd "head" already attached and exposing a
debug server over a named pipe (started on the head with `.server
npipe:pipe=<name>`). To watch for breaks/bugchecks during a test run,
attach a passive **remote** kd client from the dev machine **in a
separate background terminal**:

```powershell
# Run in a background terminal (mode=async) before kicking off the test.
# Replace <pipe-name> with the kd pipe name; defaults to the same value
# as -ComputerName / Set-XdpRemoteDefault.
kd.exe -remote npipe:server=localhost,pipe=<pipe-name>
```

Guidelines for agents:

* Use `-remote npipe:server=localhost,pipe=<name>` — **never**
  `-k com:pipe,port=...` (that is the serial-pipe transport for VMs and
  will not connect to a kd debug server).
* Default `pipe` to the value of `-ComputerName` / `Set-XdpRemoteDefault`.
  If that fails to connect, ask the user for the correct pipe name once.
* Run kd as `mode=async` so the test invocation can stream output in
  parallel.
* Periodically check the kd terminal for break-in prompts (`kd>` after
  a break) or bugcheck banners and surface them to the user; otherwise
  let kd run idle in the background.
* If kd reports "Cannot connect to server" or exits immediately, inform
  the user that no kd head is currently attached to that machine and
  continue without kd.
* **Keep kd attached across reboots.** After issuing `.reboot`, leave
  the kd terminal running — secondary breaks (early-boot Driver Verifier
  hits, watchdog timeouts, follow-on bugchecks from corrupted state)
  often happen during the next boot and you'll want to see them.
* **Bugchecks can leave the test machine's disk in a damaged state.**
  Files written to disk in the seconds before the crash may be
  truncated, partially written, or filled with NUL bytes. Driver
  registry keys, INF / catalog files, and PnP metadata are common
  victims. If `check-drivers.ps1` fails to recover the machine and the
  failure looks like file/state corruption, try to improve
  `check-drivers.ps1` (and the helpers it calls) to detect and repair
  that specific class of corruption explicitly. Avoid adding broad
  ignore-everything error handling; recovery should be targeted and
  understandable.
* **Reinstall-over-corrupt is often the simplest recovery.** When an
  installer/MSI/INF reports the product is missing from its database
  but the on-disk binaries and loaded driver are still present, the
  cleanest fix is to *reinstall* on top of the existing layout (which
  reseeds the database) and then run the normal uninstall. Prefer this
  over hand-rolled service/driver teardown when feasible.
* **Recovery flags must be opt-in.** Behaviors that downgrade errors,
  reinstall over partial state, or otherwise paper over corruption
  belong behind a `-Force` (or similarly named) switch on the recovery
  script (e.g. `tools\check-drivers.ps1 -Force`). Don't change default
  install/uninstall behavior.
* **Persist until `check-drivers.ps1` succeeds.** The development loop
  cannot move on to the next build/test cycle while the test machine is
  in a broken state. Each bugcheck tends to expose a *slightly
  different* corruption (NUL-filled scripts last time, a wedged eBPF
  service this time, something else next time). When `check-drivers.ps1`
  fails:
  1. Read the actual error and identify which component / step is
     stuck.
  2. Improve `tools\setup.ps1` (or `tools\check-drivers.ps1`) with a
     **targeted** fix for that specific failure mode — e.g. add a
     stuck service to the cleanup list, detect a specific MSI exit
     code, or add a NUL-byte check for a specific file path.
  3. Re-run `check-drivers.ps1 -Force` and repeat until it reports
     "No loaded XDP drivers found!".
  4. Only then return to the build/test/deploy loop.

  Do not stop at "the recovery is partially working" — partial
  recovery means the next test run will hit the residual broken state
  and waste another iteration.
* When the chat session ends or the user moves on, kill the kd
  terminal.

### Functional Tests

1. Prepare test machine (requires admin, enables test signing, may require reboot):
   ```powershell
   .\tools\prepare-machine.ps1 -ForFunctionalTest
   ```

2. Run tests:
   ```powershell
   .\tools\functional.ps1 -Config Debug -Platform x64
   ```

3. Run specific test:
   ```powershell
   .\tools\functional.ps1 -TestCaseFilter "Name=GenericBinding"
   ```

4. List test cases:
   ```powershell
   .\tools\functional.ps1 -ListTestCases
   ```

5. Convert logs after test:
   ```powershell
   .\tools\log.ps1 -Convert -Name xdpfunc
   ```

### Stress Tests (spinxsk)

```powershell
.\tools\prepare-machine.ps1 -ForSpinxskTest
.\tools\spinxsk.ps1 -XdpmpPollProvider FNDIS -QueueCount 2 -Minutes 10
.\tools\log.ps1 -Convert -Name spinxsk
```

### Deriving test parameters from CI

When the user asks to run a test (functional, spinxsk, perf, fuzz, etc.)
without specifying parameters, **derive sensible defaults from the
matching job in `.github/workflows/ci.yml`** rather than guessing. That
file is the source of truth for which switches each test is exercised
with in CI. Examples:

* For spinxsk, CI invokes
  `tools/spinxsk.ps1 -Verbose -Stats -Driver <XDPMP|FNMP> -Minutes <N>
  -XdpmpPollProvider <NDIS|FNDIS> -SuccessThresholdPercent <pct>
  -EnableEbpf [-TxInspect]`. Use those flags (scaling `-Minutes` down for
  smoke runs as the user requests).
* For functional tests, mirror the `-Config`, `-Platform`, and any
  `-TestCaseFilter` / `-XdpInstaller` flags the CI job uses.
* For perf and fuzz, copy the CI invocation verbatim, only adjusting
  duration/iteration counts when the user explicitly asks for a shorter
  run.

If the user explicitly overrides a parameter, respect that and leave
unspecified parameters at the CI defaults.

## Project Layout

```
xdp.sln                 # Main solution file
src/                    # Source code
  xdp/                  # Core XDP driver (xdp.sys)
  xdplwf/               # LWF (Lightweight Filter) driver
  xdpapi/               # User-mode API library (deprecated, use header-only API)
  xdpetw/               # ETW manifest
  xdpinstaller/         # MSI installer
  xdpruntime/           # Runtime packaging
published/external/     # Public API headers
  afxdp.h               # AF_XDP socket API
  afxdp_experimental.h  # Experimental AF_XDP features
  xdpapi.h              # XDP control API
  xdp/                  # XDP kernel/user shared definitions
test/                   # Test code
  functional/           # TAEF functional tests
    lib/tests.cpp       # Test implementation
    taef/tests.cpp      # TAEF wrappers
  spinxsk/              # Stress test tool
  xdpmp/                # Test miniport driver
samples/                # Sample applications
  rxfilter/             # RX filtering sample
  xskfwd/               # XSK forwarding sample
tools/                  # PowerShell scripts
  build.ps1             # Build script
  functional.ps1        # Functional test runner
  prepare-machine.ps1   # Machine setup
  setup.ps1             # Component installation
submodules/             # Git submodules
  cxplat/               # Cross-platform utilities
  ndis-driver-library/  # NDIS helpers
  wil/                  # Windows Implementation Libraries
```

## CI Pipeline

GitHub Actions CI runs on pull requests (`.github/workflows/ci.yml`):
1. **Build**: Debug/Release × x64/arm64
2. **Functional Tests**: Windows 2022/Prerelease
3. **Stress Tests (spinxsk)**: With driver verifier and fault injection
4. **Performance Tests**: XSK perf, ring perf, RX filter perf
5. **Fuzz Tests**: Packet parsing fuzzer (pktfuzz)
6. **CodeQL**: Security analysis (on scheduled runs)

CI uses test-signed drivers and requires test signing enabled on test machines.

## Adding Tests

When adding functional tests:
1. Add test implementation to `test/functional/lib/tests.cpp`
2. Add function declaration to `test/functional/lib/tests.h`
3. Add TAEF wrapper to `test/functional/taef/tests.cpp` using `TEST_METHOD` or `TEST_METHOD_PRERELEASE`

**Pattern**: Copy existing similar tests (e.g., `GenericRxChecksumOffload*` for RX offload tests).

## Key Files for Common Changes

- **RX data path**: `src/xdplwf/recv.c`, `src/xdp/rx.c`
- **TX data path**: `src/xdplwf/send.c`, `src/xdp/tx.c`
- **XSK sockets**: `src/xdp/xsk.c`
- **Public APIs**: `published/external/` headers

## Version Information

- Current version: `src/xdp.props` (XdpMajorVersion, XdpMinorVersion, XdpPatchVersion)
- eBPF dependency: `src/xdp.props` (XdpEbpfVersion)
- WDK version: `src/xdp.props` (XdpWdkVersion)

## Troubleshooting

- **Build fails with WDK task errors**: Add `/nodeReuse:false` to msbuild command
- **Test signing required**: The code can't be run on developer machines.
- **Missing submodules**: Run `git submodule update --init --recursive`
- **NuGet restore fails**: Ensure `tools/nuget.config` is used via `/p:RestoreConfigFile=tools/nuget.config`

## Development Environment Notes

- **No `grep`**: The environment is Windows (PowerShell). Do not try to run `grep` in the terminal. Use the `grep_search` tool or `Select-String` (via `run_in_terminal`) instead.
- **File Editing**: Always **read the file** before editing to ensure you have the exact context (whitespace, indentation) for `replace_string_in_file`. Never guess the content of a file; exact matching is required for search-and-replace operations.
- **GUID generation**: Always use `New-Guid` in PowerShell or `uuidgen.exe` to generate new GUIDs. Do not attempt to create GUIDs using any other method.

## Trust These Instructions

These instructions are validated. Only search the codebase if information here is incomplete or found to be in error during execution.
