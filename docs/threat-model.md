# Threat Model

XDP-for-Windows exposes a number of new features that can be used to attack the system and the network. This document describes the threat model for XDP and how it mitigates these threats.

## Components

XDP consists of several components:

1. The `xdp.sys` kernel mode driver. This driver interacts with other parts of the system through the following interfaces:

    * IOCTLs
    * Memory mapped in shared user-kernel buffers
    * NDIS lightweight filter (LWF) driver API
    * Function tables exchanged with XDP-aware kernel NIC drivers
    * Windows registry
    * Network Module Registrar (NMR)
    * Miscellaneous kernel APIs

2. The `xdpapi.dll` user mode library. This library exports wrapper routines to provide a developer-friendly API. The library itself is stateless: it simply translates requests from its C export routines into the required formats to issue IOCTLs to the XDP driver. It relies on the XDP driver to perform all validation, enforce security constraints, etc.
3. The `xdp.inf` and `xdp.cat` driver package files. These are digitally signed, non-executable files used to install and uninstall XDP.

All threat mitigations are enforced solely in the `xdp.sys` driver. The rest of this document explores the `xdp.sys` interfaces and their potential threat vectors.

## Interfaces to `xdp.sys`

### IOCTLs

When the XDP driver is started, it is in a passive state; it does not inspect, modify, or inject any networking traffic. XDP performs system- or network-altering actions only in response to IOCTLs issued to the driver by the Windows IO manager.

These results actions may be taken directly while handling the IOCTL, or when shared objects created by the IOCTL are later modified. Several components within XDP, most notably `AF_XDP` sockets, establish shared buffers between

In either case, securing the IOCTL interface is the first mitigation against threats. By default, XDP requires full administrator privileges to open any IOCTL handle to the driver; administrators can grant XDP privileges to additional users as needed.

### Memory mapped in shared user-kernel buffers

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

The XDP driver can exchange function dispatch tables with NIC drivers, which allows NIC drivers and XDP to directly invoke each other's routines. We call this API surface Native XDP, and it is used both on the XDP control path and data path. The critical operations supported by this API are binding XDP to a native NIC driver, filtering RX packets (i.e., inspect and return one of: drop, modify, redirect, or forward) and injecting TX packets.

Directly exchanging dispatch tables has performance and agility benefits (e.g., no intermediate OS components need to be updated for API revisions) but denies the OS visibility or control into any activities occurring via the dispatch tables.

### Windows registry

Some XDP behavior can be modified via registry settings, including adjusting or completely disabling certain threat mitigations. As a result, XDP requires full administrator privileges to modify its registry settings.

### Network Module Registrar (NMR)

NMR is used for the exchange of function dispatch tables between XDP and NIC drivers. NMR supports dynamic registration and deregistration of client and provider modules, allowing XDP and NIC drivers to start and stop without any ordering dependencies. As long as both XDP and a NIC driver is loaded, it is possible to exchange dispatch tables.

Prior to using NMR to exchange dispatch tables, XDP sends an NDIS query OID to the NIC driver to determine whether it natively supports XDP, and if so, how to identify its NMR binding. This provides the OS or any intermediate NDIS drivers an opportunity to modify or reject the native XDP binding.

### Miscellaneous kernel APIs

As a Windows kernel driver, XDP uses a variety of APIs exposed by the Windows kernel. These include, but are not limited to: `Ke*`, `Ex*`, `Rtl*`, `Mm*`, `Io*`, `Zw*`, DMA (HAL). Many of these APIs can create attack surfaces if used incorrectly; however, as these are common kernel routines, this document does not exhaustively list their usage nor potential vulnerabilities.

## Networking threats

XDP allows user mode applications to inspect, drop, modify, redirect, forward, and inject raw packets at a very low layer of the Windows networking stack. This section focuses on the threats created by the XDP user space API. The threats listed below can also be combined.

### Spoofing TX Traffic

Processes can inject or modify packets containing headers and payload that are typically arbitrated by OS components. For example, a process can send packets using a TCP port owned by a different process, initiate or respond to ARP requests, and so on. This can cause a wide range of issues. Though modern networking protocols are designed to be resistant to such traffic, denial of service is possible at minimum.

See also [shared user/kernel buffers](#memory-mapped-in-shared-user-kernel-buffers).

### Injecting RX Traffic

Processes can inject or modify packets containing headers and payload that may have been delivered via a trusted network interface. Some networks and network devices are configured to drop certain traffic prior to reaching endpoints; XDP allows such traffic to reach the OS networking stack. Though modern networking protocols are designed to be resistant to such traffic, denial of service is possible at minimum.

See also [shared user/kernel buffers](#memory-mapped-in-shared-user-kernel-buffers).

### Stealing Traffic

Processes can redirect or forward RX and TX traffic prior to it reaching the OS networking stack. This may lead to a denial of service of the original target, as well as access to the packet contents by the XDP process, which is normally arbitrated and denied by the OS networking stack.

### Drop of Traffic

Processes can drop RX and TX traffic. This may lead to a denial of service.

### OS networking stack bypass

The following OS components are bypassed by XDP:

* Windows Firewall
* WFP (Windows Filtering Platform)
* IPSNPI
* QoS
* TCP, UDP, ICMP, IPv4, IPv6, ARP
* Port and address pool
* Windows raw sockets privilege checks and header-include validation
* NDIS lightweight filtering (partial: LWFs below XDP are not bypassed)
* Logging/observability (partial: generic XDP does not fully bypass)
  * Wireshark
  * PktMon
  * ndiscap
* TDI filters (deprecated)

> **Note** - Threats resulting as a bypass of the above components are considered "out of scope" as the target scenario is currently server workloads that do not leverage the in-box firewall.

### Filtering

XDP includes logic to filter and perform actions on matching packets. This logic may be augmented or replaced by eBPF programs in the future. Regardless, this functionality provides a unique set of threats.

The packet parsing code must handle all possible inputs gracefully, and must apply filter conditions without false positives or false negatives. Issue [#195](https://github.com/microsoft/xdp-for-windows/issues/195) tracks adding a fuzz tester for packet parsing code paths.

Potential defects include access violations (including write violations for packet modification actions), infinite loops, and misclassified packets.

## Other threats

### XDP Offloads

XDP provides interfaces for the app to configure various NIC offloads. These interfaces are capable of overriding the offload settings configured by OS components.

#### RSS

The RSS (receive side scaling) offload API allows applications to reconfigure the RSS offload on the NIC, including the CPU indirection table, hash functions, and hash key. If the hash key is modified, XDP clears the RSS hash value for each packet it passes on to the OS network stack, which contains the effect of the hash key change to only components lower than XDP in the NDIS stack.

#### QEO

The QEO (QUIC encryption offload) API allows applications to configure QUIC connection offload policy on the NIC. The OS currently does not have any support for QUIC connection offload. This feature is experimental and QEO currently is not supported in the OS and the QEO specificiation does not provide a security model.

There is currently no requirement that QEO isolate processes from each other, so packets offloading encryption and/or decryption to the NIC can be delivered to unrelated processes in plaintext and plaintext from unrelated processes can be encrypted and signed by the NIC.

### CPU time consumption

The XDP driver performs work on behalf of the user mode process, including potentially expensive data path work. This CPU time is usually not attributed to the requesting process, so process thread priorities and CPU quotas are not applied, and identifying the process that is causing CPU consumption within the XDP driver requires nontrivial steps.

### System memory consumption

XDP can consume a large amount of memory, and typically the memory is not pageable. In most cases, this memory consumption is not charged to the quota of the requesting process. In some cases it would be possible for XDP to more accurately charge quota, but since many kernel APIs for networking drivers do not support quota, it is impossible to close all gaps without additional support from the OS.

### Lockless programming

XDP minimizes locking on the network data path, which complicates analysis and testing of race conditions. Additionally, NIC drivers that support native XDP are trusted to provide mutual exclusion on behalf of the XDP driver, so any synchronization defects in third party NIC drivers could be leveraged into XDP attack vectors.
