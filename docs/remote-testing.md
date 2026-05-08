# Running XDP tests on a remote machine

The XDP test scripts (`tools/functional.ps1`, `tools/spinxsk.ps1`, ...) cannot
run on a developer machine because they require test-signed drivers and a
clean networking state. To make iterating against a separate test machine
seamless, the scripts integrate with `tools/remote.ps1`, which uses
PowerShell remoting (WinRM) to deploy and execute.

## One-time setup on the test machine

Run this once on the test machine in an **elevated** PowerShell:

```powershell
.\tools\remote.ps1 -EnableRemoting
```

This is equivalent to `Enable-PSRemoting -Force -SkipNetworkProfileCheck`,
plus a few WinRM tweaks (larger envelope, longer idle timeout) so that
deployment and long test runs complete cleanly.

If the dev box is not in the same domain as the test machine, the dev script
will automatically add the test machine to the local WSMan TrustedHosts list
on first use. This requires the dev-machine PowerShell to be elevated. If
you cannot run elevated, do this manually once per test machine:

```powershell
Set-Item WSMan:\localhost\Client\TrustedHosts -Value '<TestMachineNameOrIp>' -Concatenate -Force
```

## Day-to-day workflow

From the dev machine, after a normal `tools\build.ps1`:

```powershell
# Push tools + artifacts to the remote (one-time after each build):
.\tools\remote.ps1 -Deploy -ComputerName test-vm-1

# Run any test script as you would locally; it auto-forwards to the remote:
.\tools\functional.ps1 -ComputerName test-vm-1
.\tools\functional.ps1 -ComputerName test-vm-1 -TestCaseFilter "Name=GenericBinding"
.\tools\spinxsk.ps1   -ComputerName test-vm-1 -Minutes 5
```

Output, verbose, warning, and error streams stream back to the dev console
and `$LASTEXITCODE` is propagated.

### Common options

| Parameter | Default | Notes |
|---|---|---|
| `-ComputerName` | (none) | Hostname / FQDN / IP of the test machine. When unset, the script runs locally as before. |
| `-Credential` | current user | Optional `PSCredential`. |
| `-RemoteRoot` | `C:\xdp` | Workspace root on the remote machine. |
| `-SkipDeploy` | off | Skip the file deploy step (re-use whatever is already on the remote). Faster when iterating. |

### Iterating quickly

For script-only changes (no rebuild) you can deploy just the scripts:

```powershell
.\tools\remote.ps1 -Deploy -ComputerName test-vm-1 -SkipArtifacts
```

For test reruns where you know the remote already has the right binaries:

```powershell
.\tools\functional.ps1 -ComputerName test-vm-1 -SkipDeploy
```

### Running arbitrary scripts remotely

`remote.ps1 -Invoke` runs any repo script on the remote and forwards
output:

```powershell
.\tools\remote.ps1 -Invoke -ComputerName test-vm-1 `
    -ScriptPath tools\functional.ps1 `
    -ArgumentList @{ TestCaseFilter = 'Name=GenericBinding' }
```

## How it works

`tools\remote.ps1`:

* `-EnableRemoting` configures WinRM on the test machine.
* `-Deploy` copies `tools\`, `src\xdp.props`, and the relevant
  `artifacts\bin\<plat>_<cfg>\` plus dependency directories
  (`corenet-ci-*`, `nuget`, `fn`, `Microsoft.TestPlatform`, `ebpfmsi`)
  to the remote via `Copy-Item -ToSession`. The remote layout mirrors
  the local repo layout so all existing scripts work unchanged.
* `-Invoke` runs the requested script under `$RemoteRoot`. Streams flow
  back to the dev console, and `$LASTEXITCODE` propagates.

The forwarding from `functional.ps1` / `spinxsk.ps1` is implemented in
`Invoke-XdpRemoteIfRequested` in `tools\common.ps1`; adding remote
support to another script is a one-liner: declare `-ComputerName /
-Credential / -RemoteRoot / -SkipDeploy` parameters and call
`Invoke-XdpRemoteIfRequested` immediately after dot-sourcing
`common.ps1`.
