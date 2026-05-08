# XDP for Windows: Architecture Overview

## Introduction
XDP for Windows is a high-performance packet processing framework inspired by Linux XDP, designed to enable fast, programmable networking on Windows by bypassing most of the traditional OS networking stack. It provides both kernel and user mode components, supporting advanced use cases such as custom packet filtering, forwarding, and user-mode networking via AF_XDP sockets.

## Key Components

### 1. xdp.sys (Kernel Driver)
- The core of XDP for Windows, implemented as a Windows kernel driver.
- Hooks into the Windows networking stack at the NDIS (Network Driver Interface Specification) layer.
- Supports two modes:
  - **Generic XDP**: Works with any NDIS interface, no NIC driver changes required.
  - **Native XDP**: Requires NIC driver support for maximum performance and direct data path access.
- Provides programmable hook points for packet inspection, modification, and redirection.
- Implements shared memory rings for high-speed packet transfer between kernel and user mode.
- Supports eBPF (experimental) for programmable packet processing.

### 2. XDP API Headers (User Mode APIs)
- Header-only APIs that expose the XDP API to applications (available for `XDP_API_VERSION_3` or later).
- Inline functions translate API calls into IOCTLs and manage shared memory with the kernel driver.
- Used by applications to configure XDP programs, manage queues, and interact with AF_XDP sockets.
- Applications can freely compile these header-only implementations into their own user or kernel mode code.
- **Backward Compatibility**: For applications using `XDP_API_VERSION_1` or `XDP_API_VERSION_2`, a stateless user-mode library (`xdpapi.dll`) is provided but is deprecated. New applications should use `XDP_API_VERSION_3` or later.

### 3. AF_XDP Sockets
- User-mode API for high-speed packet I/O, similar to Linux AF_XDP.
- Applications can send/receive raw L2 frames using shared memory rings.
- Enables user-mode networking applications (e.g., packet capture, custom forwarding, DPDK-like use cases).

### 4. XDPIF Provider (Internal Interface Abstraction)
- Abstracts the details of different network interface types.
- Notifies core XDP when interfaces are added/removed and provides capability information.

### 5. Control and Data Path APIs
- Control path: IOCTLs for configuration, program loading, and queue management.
- Data path: Shared memory rings for fast packet transfer, with minimal kernel intervention.

## Data Flow
1. **Packet Reception**: Packets arrive at the NIC and are delivered to the NDIS layer.
2. **XDP Hook**: The XDP driver intercepts packets at configured hook points (L2, RX, TX, etc.).
3. **Program Execution**: XDP programs (native or eBPF) inspect, filter, or modify packets.
4. **Action**: Packets can be dropped, passed up the stack, redirected to user mode (AF_XDP), or forwarded.
5. **User Mode**: Applications interact with AF_XDP sockets via shared memory rings for packet processing.

## Security and Access Control
- Only administrators (or explicitly authorized users) can configure XDP or access AF_XDP sockets.
- Shared memory regions are carefully managed to avoid privilege escalation or data leakage.
- The driver is signed and uses standard Windows security mechanisms.

## Extensibility
- XDP for Windows is designed to be extensible via eBPF (experimental) and a primitive built-in rules-based program engine.
- New hook points and interface types can be added as needed.

## Comparison to Linux XDP
- Inspired by Linux XDP, but adapted for Windows kernel and driver architecture.
- Not source compatible, but similar in spirit and API design.
- Provides both generic and native modes, like Linux XDP.
- AF_XDP sockets API is similar but not identical to Linux.

## References
- [Usage Guide](./usage.md)
- [AF_XDP API](./afxdp.md)
- [FAQ](./faq.md)
- [Threat Model](./threat-model.md)

---
This document provides a high-level overview. For API details, see the `docs/api/` directory.
