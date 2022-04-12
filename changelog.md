# Changelog

All notable changes to this project will be documented in this file.

## [0.16.0] - 2022-04-12

### Added

- (XSK apps) Add RSS configuration API.

### Changed

- (Native drivers) Rename `XDP_RING::NextIndex` to `XDP_RING::InterfaceReserved`
- (Native drivers) Rename `XdpReceiveBatch` to `XdpReceive`
- (Native drivers) `XDP_ACTIVATE_RX_QUEUE` and `XDP_ACTIVATE_TX_QUEUE` now return `NTSTATUS` instead
  of `VOID`.

### Removed

- (Native drivers) Remove `XdpReceive` single-frame inspection API. Use the
  batched receive API instead.
- (Native drivers) Remove `XDP_RX_ACTION_PEND`.
