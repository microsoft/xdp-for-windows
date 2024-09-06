# Registry Keys

XDP-for-Windows exposes several configuration options via registry keys. These keys are by default experimental and subject to change in minor releases; officially supported keys are explicitly marked and will remain supported for the lifetime of a major release.

## `HKLM\SYSTEM\CurrentControlSet\Services\xdp`

Options under this registry key are removed when XDP is uninstalled and must be reconfigured after reinstallation or version upgrades.

| Name               | Type    | Default | Options     | Description                                                                                                  |
|--------------------|---------|---------|-------------|--------------------------------------------------------------------------------------------------------------|
| VerboseOn          | `DWORD` | `0`     | `[0, 1]`    | `1` enables verbose always-on IFR logging.                                                                   |
| LogPages           | `DWORD` | `1`     | `[1, 16]`   | Number of pages for always-on IFR logging.                                                                   |
| XdpEbpfEnabled     | `DWORD` | `0`     | `[0, 1]`    | `1` enables attaching eBPF programs.                                                                         |
| XdpEbpfMode        | `DWORD` | N/A     | `[0, 1]`    | `0` forces eBPF programs to attach in generic mode.<br>`1` forces eBPF programs to attach in native mode.    |
| XdpFaultInject     | `DWORD` | `0`     | `[0, 1]`    | `1` enables randomized fault injection. Only implemented in debug builds.                                    |
| XdpRxRingSize      | `DWORD` | `32`    | `[8, 8192]` | Number of frames (or min. fragments) in XDP kernel receive rings.<br>Must be a power of two.                 |
| XdpTxRingSize      | `DWORD` | `32`    | `[8, 8192]` | Minimum frames in XDP kernel transmit rings.<br>Must be a power of two.                                      |
| XskDisableTxBounce | `DWORD` | `0`     | `[0, 1]`    | `1` disables copying UMEM transmit buffers into kernel-only buffers.                                         |
| XskRxZeroCopy      | `DWORD` | `0`     | `[0, 1]`    | `1` disables copying kernel-only receive buffers into UMEM buffers. This is only useful for microbenchmarks. |
