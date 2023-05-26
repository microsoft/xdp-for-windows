# Threat Model

XDP-for-Windows exposes a number of new features that can be used to attack the system and the network. This document describes the threat model for XDP and how it mitigates these threats.

## Components

XDP consists of several components:

1. The `xdp.sys` kernel mode driver. This driver interacts with other parts of the system through the following interfaces:

    * IOCTLs
    * Memory mapped in shared user/kernel buffers
    * NDIS lightweight filter (LWF) driver API
    * Function tables exchanged with XDP-aware kernel NIC drivers
    * Windows registry
    * Network Module Registrar (NMR)
    * Miscellaneous kernel APIs (including `Ke*`, `Ex*`, `Rtl*`, `Mm*`, `Io*`, `Zw*`, DMA, and more APIs)

2. The `xdpapi.dll` user mode library. This library exports wrapper routines to provide a developer-friendly API. The library itself is stateless: it simply translates requests from its C export routines into the required formats to issue IOCTLs to the XDP driver. It relies on the XDP driver to perform all validation, enforce security constraints, etc.
3. The `xdp.inf` and `xdp.cat` driver package files. These are digitally signed, non-executable files used to install and uninstall XDP.

All threat mitigations are enforced solely in the `xdp.sys` driver. The rest of this document explores the `xdp.sys` interfaces and their potential threat vectors.

## Interfaces to `xdp.sys`

### IOCTLs

When the XDP driver is started, it is in a passive state; it does not inspect, modify, or inject any networking traffic. XDP performs system- or network-altering actions only in response to IOCTLs issued to the driver by the Windows IO manager.

These results actions may be taken directly while handling the IOCTL, or when shared objects created by the IOCTL are later modified. Several components within XDP, most notably `AF_XDP` sockets, establish shared buffers between

In either case, securing the IOCTL interface is the first mitigation against threats. Today, XDP requires full administrator privileges to open any IOCTL handle to the driver.

### Memory mapped in shared user/kernel buffers

To optimize performance, XDP establishes shared memory buffers between user and kernel mode contexts. These shared buffers include `AF_XDP` packet descriptor rings, `AF_XDP` packet data buffers, and `XDP` program port filtering tables. The `AF_XDP` packet descriptor rings and `XDP` program port tables are provided by user mode and mapped into kernel memory by XDP, while `AF_XDP` descriptor rings are allocated by XDP and mapped into user virtual memory.

In general, shared buffers expose time-of-check to time-of-use (TOCTOU) race conditions, which must be handled carefully by XDP and any other kernel component XDP exposes the mapped buffers to. XDP uses a variety of mitigations to this threat, including volatile accessors and memory barriers provided by the OS, and, when necessary, volatile copies from shared buffers into trusted kernel-only buffers.

NDIS, in particular, has ambiguous requirements for immutability of packet contents. Components throughout the Windows network stack generally expect protocol headers to be immutable, but there are precedents where the protocol payload of packets (i.e., data beyond the headers) is liable to be modified by untrusted components at any time. Since XDP does not evaluate headers for protocol correctness, it cannot deduce where this trust boundary lies in each packet. As a result, the XDP driver copies all packet data from shared buffers into kernel-only buffers before providing a packet to NDIS. For performance reasons, this mitigation may be disabled with the `XskDisableTxBounce` registry setting.

### NDIS lightweight filter (LWF) driver API

The XDP driver uses the NDIS LWF programming interface to do the following, according to requests from user mode:

* inspect packets
* inject packets
* drop packets
* inspect OID requests (an OID request is an IOCTL between NDIS drivers)
* inject OID requests
* complete OID requests

NDIS LWF drivers must also perform a variety of bookkeeping actions. The required bookkeeping is known to be somewhat error prone, and defects can cause a wide range of undefined behavior.

With a handful of narrow exceptions, all LWF actions are fully trusted by NDIS, so XDP is responsible for validating user mode requests and enforcing correct, secure behavior prior to performing a LWF action.

### Function tables exchanged with XDP-aware kernel NIC drivers

TODO

### Windows registry

Some XDP behavior can be modified via registry settings, including adjusting or completely disabling certain threat mitigations. As a result, XDP requires full administrator privileges to modify its registry settings.

### Network Module Registrar (NMR)

TODO

### Miscellaneous kernel APIs

TODO

## Networking threats

TODO

### Spoofing TX Traffic

TODO

### Injecting RX Traffic

TODO

### Stealing RX Traffic

TODO

### Inspection or Drop of Traffic

TODO

### Modifying RX Traffic

TODO

### OS networking stack bypass

> **Note** - Currently considered "out of scope" as the target scenario is currently server workloads that do not leverage the in-box firewall.

* Firewall
* WFP (Windows Filtering Platform)
* IPSNPI bypass
* QoS bypass
* Port and address pool bypass
* NDIS lightweight filtering (partial: LWFs below XDP are not bypassed)
* Logging/observation bypass (partial: generic XDP does not bypass)

### Filtering

XDP-for-Windows has built in logic to match and filter packets on the RX path. This logic may be augmented or replaced by eBPF programs in the future. Regardless, this functionality provides a unique set of threats.

## Other threats

### XDP Offloads

In addition to the normal datapath flows, XDP-for-Windows provides interfaces for the app to configure various offloads, such as RSS, with the goal of improving performance of the system.

#### Configuring RSS

TODO

#### Plumbing QEO

TODO

### System memory consumption

TODO

### Lockless programming

XDP minimizes locking on the network data path, which complicates analysis and testing of race conditions. Additionally, NIC drivers that support native XDP are trusted to provide mutual exclusion on behalf of the XDP driver, so any synchronization defects in third party NIC drivers could be leveraged into XDP attack vectors.

## TODO

- Packet contents TOCTOU
- Packet parsing (read violation, infinite loop, etc.)
- Packet rewriting (write violation, information disclosure, etc.)
- IOCTL surface
- Memory mapping into process
- Locked memory exhaustion (no quotas)
- Elevation of thread priority?
- WFP bypass, including Windows Firewall
- QEO silent failure to encrypt or misdelivery of decrypted payload
- Local DoS (drop packets, consume CPU, inject expensive packets, etc.)
- Spoofing/repudiation
