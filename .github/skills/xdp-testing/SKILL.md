---
name: xdp-testing
description: 'XDP-for-Windows test execution and debugging workflow. Use when running tests (functional, spinxsk, pktfuzz, ringperf, rxfilter, xskmaprx, xskfwdkm), debugging bugchecks, attaching kd, recovering test machines after crashes, or invoking any tools/*.ps1 script that runs on hardware. Covers PowerShell remoting via -ComputerName, kd remote pipe debugging, and the inject/build/bugcheck/.reboot/check-drivers recovery loop.'
user-invocable: false
---

# XDP for Windows: Testing & Debugging

This skill covers everything an agent needs to execute tests on the
required separate test machine, attach kd for crash analysis, and
recover the machine to a known-good state after a bugcheck.

> [!IMPORTANT]
> All XDP testing must occur on a separate Windows test machine — never
> the dev machine. Always pass `-ComputerName <machine>` explicitly on
> every test command. Do **not** rely on
> `tools\remote-set-default.ps1` alone — the session default lives in
> the PowerShell global scope and is silently lost when a new terminal
> spawns (after `Start-Process`, kd sessions, or whenever the tool host
> opens a fresh shell). A test script invoked without `-ComputerName`
> and without an active session default will run **against the dev
> machine** and may damage local state. Treat `-ComputerName` as
> required.

## When to Use

- Running any of: `functional.ps1`, `spinxsk.ps1`, `prepare-machine.ps1`
  (non-build scenarios), `check-drivers.ps1`, `rxfilter.ps1`,
  `rxfilterperf.ps1`, `ringperf.ps1`, `xskmaprx.ps1`, `xskfwdkm.ps1`,
  `pktfuzz.ps1`.
- Triggering, observing, or analyzing a kernel bugcheck.
- Recovering a test machine that's stuck after a crash.
- Choosing default parameters for a test invocation.

## Build-only scripts (always local, ignore -ComputerName)

`build.ps1`, `merge-artifacts.ps1`, `publish-nuget.ps1`,
`prepare-machine.ps1 -ForBuild` / `-ForEbpfBuild`,
`create-test-archive.ps1`, `update-nuspec.ps1`.

## Remote-test workflow

1. **Ask the user once per chat session** which test machine to use:
   *"Which test machine should I run tests on?"*. Accept hostname,
   FQDN, or IP. Remember the value for the rest of the session and
   pass it as `-ComputerName <machine>` on every test invocation.

2. *(Optional, human convenience only)* set the session default. Does
   **not** replace `-ComputerName` for agent-driven runs:
   ```powershell
   .\tools\remote-set-default.ps1 <machine>
   ```

3. **Run any test script with `-ComputerName`** — it auto-deploys
   `artifacts\bin\<plat>_<cfg>\` and streams output back:
   ```powershell
   .\tools\functional.ps1 -ComputerName <machine> -ListTestCases
   .\tools\functional.ps1 -ComputerName <machine> -TestCaseFilter "Name=GenericBinding"
   .\tools\spinxsk.ps1   -ComputerName <machine> -Minutes 5
   ```

4. **Credentials.** First connect auto-adds the host to WSMan
   TrustedHosts (UAC prompt) and, if WinRM rejects with auth error,
   prompts at the **console** (`Read-Host`, no GUI) for username and
   password. Credentials are cached in
   `$global:XdpRemoteCredentialCache` for the rest of the session. Do
   **not** generate `Get-Credential` calls or store passwords in
   scripts.

5. **One-time test-machine setup** (the user must run, requires
   elevation):
   ```powershell
   .\tools\remote.ps1 -EnableRemoting
   ```
   If the user hasn't done this, the connection fails with "WinRM
   client cannot process the request" — surface this and ask them to
   run it.

6. **Build first.** Always run `.\tools\build.ps1` (or rely on a
   recent build) before remote test commands, since deploy pulls from
   `artifacts\bin\<plat>_<cfg>\`.

## Choosing test parameters

When the user doesn't specify parameters, **derive sensible defaults
from the matching job in [.github/workflows/ci.yml](../../workflows/ci.yml)**.
That file is the source of truth for which switches each test exercises
in CI.

* spinxsk: `tools/spinxsk.ps1 -Verbose -Stats -Driver <XDPMP|FNMP>
  -Minutes <N> -XdpmpPollProvider <NDIS|FNDIS>
  -SuccessThresholdPercent <pct> -EnableEbpf [-TxInspect]`. Scale
  `-Minutes` down for smoke runs as requested.
* functional: mirror `-Config`, `-Platform`, and any
  `-TestCaseFilter` / `-XdpInstaller` flags from the CI job.
* perf/fuzz: copy the CI invocation verbatim, adjusting only
  duration/iteration counts when the user asks for a shorter run.

Respect explicit user overrides; leave everything else at CI defaults.

## Kernel debugger (kd) attach for bugcheck monitoring

Test machines typically have a kd "head" already attached and exposing
a debug server over a named pipe (started on the head with
`.server npipe:pipe=<name>`). To watch for breaks during a test run,
attach a passive **remote** kd client from the dev machine in a
**separate background terminal** before kicking off the test:

```powershell
# Run with mode=async; <pipe-name> defaults to the same value as
# -ComputerName / Set-XdpRemoteDefault.
kd.exe -remote npipe:server=localhost,pipe=<pipe-name>
```

### kd usage rules

* **Always** `-remote npipe:server=localhost,pipe=<name>` — never
  `-k com:pipe,port=...` (that's the serial-pipe transport for VMs and
  will not connect to a kd debug server).
* Default `pipe` to the value of `-ComputerName` /
  `Set-XdpRemoteDefault`. If that fails to connect, ask the user once
  for the correct pipe name.
* Run kd `mode=async` so test invocations stream output in parallel.
* Periodically check the kd terminal for break-in prompts (`kd>` after
  a break) or bugcheck banners; surface them to the user, otherwise
  let kd run idle.
* If kd reports "Cannot connect to server" or exits immediately, no kd
  head is currently attached. Inform the user and continue without kd.
* **Keep kd attached across reboots.** After `.reboot`, leave the kd
  terminal running — secondary breaks (early-boot Driver Verifier hits,
  watchdog timeouts, follow-on bugchecks from corrupted state) often
  happen during the next boot.
* **Issuing `.reboot` non-interactively from the dev machine.** Use
  `kd.exe -cf <scriptfile>` with a hidden window:
  ```powershell
  $cmd = New-TemporaryFile
  Set-Content -Path $cmd -Value ".reboot`r`nqd"
  Start-Process kd.exe -ArgumentList @(
      "-remote","npipe:server=localhost,pipe=<machine>",
      "-cf",$cmd.FullName) -PassThru -WindowStyle Hidden |
      ForEach-Object { $_.WaitForExit(60000) | Out-Null }
  Remove-Item $cmd.FullName -Force
  ```
  Do not try to drive an interactive `kd>` prompt via PowerShell's
  terminal stdin — that connects you to the *host* shell, not kd.
  Then poll `Test-WSMan -ComputerName <machine>` until it succeeds.
* When the chat session ends or the user moves on, kill the kd
  terminal.

## Bugcheck recovery loop

Bugchecks frequently leave the test machine in a partially-corrupted
state that prevents the next iteration from running. The recovery
script is `tools\check-drivers.ps1 -Force`. **The dev loop cannot move
on until that command reports "No loaded XDP drivers found!".**

### Recovery doctrine

* **Bugchecks can corrupt on-disk and registry state.** Files written
  in the seconds before the crash may be truncated, partially written,
  or filled with NUL bytes. Driver registry keys, INF/catalog files,
  PnP metadata, and Windows Installer product databases are common
  victims.
* **Reinstall over partial state when feasible.** When an
  installer/MSI/INF reports the product is missing from its database
  but the on-disk binaries are still present, *reinstall* on top of
  the existing layout (which reseeds the database) and then run the
  normal uninstall. Prefer this over hand-rolled service/driver
  teardown.
* **Recovery flags MUST be opt-in.** Behaviors that downgrade errors,
  reinstall over corruption, or paper over partial state belong
  behind a `-Force` switch on the recovery script (e.g.
  `tools\check-drivers.ps1 -Force`). Don't change default
  install/uninstall behavior — that would silently mask real bugs.
* **Persist until clean.** Each bugcheck tends to expose a *slightly
  different* corruption (NUL-filled scripts last time, wedged eBPF
  service this time, something else next time). When
  `check-drivers.ps1` fails:
  1. Read the actual error and identify which component / step is
     stuck.
  2. Improve `tools\setup.ps1` (or `tools\check-drivers.ps1`) with a
     **targeted** fix for that specific failure mode — add a stuck
     service to the cleanup list, detect a specific MSI exit code,
     add a NUL-byte check for a specific file path, etc.
  3. Re-run `check-drivers.ps1 -Force` and repeat until it succeeds.
  4. Only then return to the build/test/deploy loop.

  Do not stop at "the recovery is partially working" — partial
  recovery means the next test run will hit the residual broken
  state and waste an iteration. Avoid broad ignore-everything error
  handling; recovery should be targeted and understandable.

## Common test commands

```powershell
# Functional tests
.\tools\prepare-machine.ps1 -ComputerName <machine> -ForFunctionalTest
.\tools\functional.ps1 -ComputerName <machine> -Config Debug -Platform x64
.\tools\functional.ps1 -ComputerName <machine> -TestCaseFilter "Name=GenericBinding"
.\tools\functional.ps1 -ComputerName <machine> -ListTestCases
.\tools\log.ps1 -Convert -Name xdpfunc

# Stress tests (spinxsk)
.\tools\prepare-machine.ps1 -ComputerName <machine> -ForSpinxskTest
.\tools\spinxsk.ps1 -ComputerName <machine> -XdpmpPollProvider FNDIS -QueueCount 2 -Minutes 10
.\tools\log.ps1 -Convert -Name spinxsk

# Recovery
.\tools\check-drivers.ps1 -ComputerName <machine> -Force
```

See [docs/remote-testing.md](../../../docs/remote-testing.md) for the
human-facing reference.
