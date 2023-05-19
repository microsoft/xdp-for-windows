# Threat Model

XDP-for-Windows exposes a number of new features that can be used to attack the system. This document describes the threat model for XDP-for-Windows, and how it mitigates these threats.

## XDP Core

The core of XDP-for-Windows provides for the app to set up shared memory between the kernel and user mode, which is used to pass packets between the kernel and user mode.

### System Memory Consumption

TODO

### Spoofing TX Traffic

TODO

### Injecting RX Traffic

TODO

### Bypassing the Firewall

> **Note** - Currently considered "out of scope" as the target scenario is currently server workloads that do not leverage the in-box firewall.

## XDP Filtering

XDP-for-Windows has built in logic to match and filter packets on the RX path. This logic may be augmented or replaced by eBPF programs in the future. Regardless, this functionality provides a unique set of threats.

### Stealing RX Traffic

TODO

### Inspection or Drop of Traffic

TODO

## XDP Offloads

In addition to the normal datapath flows, XDP-for-Windows provides interfaces for the app to configure various offloads, such as RSS, with the goal of improving performance of the system.

### Configuring RSS

TODO

### Plumbing QEO

TODO
