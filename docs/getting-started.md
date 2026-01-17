# Getting Started with XDP for Windows

## What is XDP for Windows?

XDP for Windows is a high-performance packet processing framework that enables fast, programmable networking by bypassing most of the traditional Windows networking stack. Inspired by Linux XDP (eXpress Data Path), it provides developers with direct access to network packets at the lowest possible latency while maintaining system security and stability.

## Key Benefits

- **High Performance**: Bypass the kernel networking stack for minimal latency packet processing
- **Programmable**: Use eBPF programs or built-in rules to customize packet handling
- **User-Mode Networking**: AF_XDP sockets provide DPDK-like capabilities without kernel programming
- **Zero-Copy**: Shared memory rings eliminate costly data copying between kernel and user space
- **Windows Native**: Designed specifically for Windows kernel architecture and NDIS drivers

## Linux XDP vs XDP for Windows

XDP for Windows draws heavy inspiration from Linux XDP but is specifically designed for the Windows ecosystem. Here's a detailed comparison:

### Similarities
| Feature | Linux XDP | XDP for Windows |
|---------|-----------|-----------------|
| **Zero-copy packet I/O** | ✅ AF_XDP sockets | ✅ AF_XDP sockets |
| **eBPF programmability** | ✅ Full support | ✅ Experimental support |
| **Generic mode** | ✅ Works with any driver | ✅ Works with any NDIS driver |
| **Native mode** | ✅ Driver-specific optimizations | ✅ NDIS driver optimizations |
| **Packet actions** | ✅ PASS, DROP, TX, REDIRECT | ✅ PASS, DROP, L2FWD, REDIRECT |
| **User-space libraries** | ✅ libxdp, libbpf | ✅ xdpapi.dll, helper libraries |

### Key Differences
| Aspect | Linux XDP | XDP for Windows |
|--------|-----------|-----------------|
| **Kernel Integration** | Netfilter hooks, traffic control | NDIS layer hooks |
| **Driver Model** | netdev drivers | NDIS miniport drivers |
| **eBPF Runtime** | Native kernel support | [eBPF for Windows](https://github.com/microsoft/ebpf-for-windows) |
| **API Compatibility** | Linux networking APIs | Windows-specific APIs (not source compatible) |
| **Supported Platforms** | Linux kernel 4.8+ | Windows Server 2019, 2022 |
| **Programming Model** | C with libbpf, or direct eBPF | C with xdpapi.dll, experimental eBPF |

### Source Compatibility
**XDP for Windows is NOT source compatible with Linux XDP.** While the concepts and APIs are similar in spirit, the implementations differ due to fundamental differences between Linux and Windows kernel architectures. However, porting applications between the platforms is typically straightforward due to similar design patterns.

## Use Cases

XDP for Windows is ideal for:

- **High-frequency trading** - Ultra-low latency packet processing
- **Network monitoring** - Deep packet inspection without performance penalties  
- **Custom firewalls** - Programmable packet filtering at line rate
- **Load balancers** - Fast packet forwarding and NAT
- **Packet generators** - High-speed network testing tools
- **User-space networking** - Bypass the kernel for custom protocol stacks

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    User Mode Applications                    │
├─────────────────────────────────────────────────────────────┤
│  AF_XDP Sockets  │  XDP API (xdpapi.dll)  │  Helper Libs   │
├─────────────────────────────────────────────────────────────┤
│                    Windows Kernel                            │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                 XDP Driver (xdp.sys)                    │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │ │
│  │  │ Generic XDP │  │ Native XDP  │  │ Shared Memory   │ │ │
│  │  │   (Any NIC) │  │ (NIC-aware) │  │     Rings       │ │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘ │ │
│  └─────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                      NDIS Layer                              │
├─────────────────────────────────────────────────────────────┤
│                   Network Drivers                            │
└─────────────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites
- Windows Server 2019 or 2022
- Administrator privileges
- For eBPF: [eBPF for Windows](https://github.com/microsoft/ebpf-for-windows) (experimental)

### Installation
```powershell
# Download and install XDP for Windows
Invoke-WebRequest https://aka.ms/xdp-v1.msi -OutFile xdp.msi
msiexec.exe /i xdp.msi /quiet

# Optional: Enable experimental eBPF support
msiexec.exe /i xdp.msi ADDLOCAL=xdp_ebpf /quiet
```

### Your First XDP Program
1. **Create a simple packet filter** - See the [rxfilter sample](../samples/rxfilter/)
2. **Build an AF_XDP application** - See the [xskfwd sample](../samples/xskfwd/)
3. **Explore the APIs** - Check out the [API documentation](./api/)

## Next Steps

- **[Usage Guide](./usage.md)** - Detailed installation and configuration instructions
- **[Architecture](./architecture.md)** - Deep dive into XDP for Windows internals
- **[AF_XDP API](./afxdp.md)** - User-space socket programming interface
- **[Samples](../samples/)** - Example applications and code snippets
- **[FAQ](./faq.md)** - Common questions and troubleshooting

## Community and Support

- **GitHub Issues**: Report bugs and request features
- **Discussions**: Ask questions and share knowledge
- **Contributing**: See our [contribution guidelines](../readme.md#contributing)

---

Ready to get started? Check out our [usage guide](./usage.md) for detailed setup instructions, or dive into the [samples](../samples/) to see XDP in action!