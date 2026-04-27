# Deprecated Samples

> **Warning:** The samples in this directory use the **deprecated built-in rules
> engine** (`XDP_RULE`, `XDP_MATCH_TYPE`, `XdpCreateProgram`). This API is
> planned for removal. All new development should use **eBPF programs** instead.

## Affected Samples

| Sample | Description |
|--------|-------------|
| [rxfilter](rxfilter/) | RX packet filter using built-in match/action rules. |
| [xskfwd](xskfwd/) | AF_XDP forwarding using built-in `XDP_MATCH_UDP_DST` + `XDP_PROGRAM_ACTION_REDIRECT`. |
| [xskrestricted](xskrestricted/) | Restricted-token AF_XDP forwarding using built-in rules with handle duplication. |

## Migration

Replacement samples using eBPF programs are available in the parent
[samples/](../) directory. See the [eBPF Integration Guide](../../docs/ebpf.md)
and [Migrating from Built-in Rules](../../docs/ebpf.md#migrating-from-built-in-rules)
for migration guidance.
