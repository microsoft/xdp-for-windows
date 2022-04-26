# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- (AF_XDP) Add XskActivate API to enable AF_XDP transmit and receive data paths. This routine
  requires the socket is already bound to an XDP queue using XskBind.

### Changed

- (AF_XDP) The XskBind API no longer requires descriptor rings be configured and no longer activates
  the data path. Instead, applications indicate whether RX and/or TX should be bound using flags.

## [0.16.0] - 2022-04-12

### Added

- (AF_XDP) Add RSS configuration API.

### Changed

- (Native drivers) Rename `XDP_RING::NextIndex` to `XDP_RING::InterfaceReserved`
- (Native drivers) Rename `XdpReceiveBatch` to `XdpReceive`
- (Native drivers) `XDP_ACTIVATE_RX_QUEUE` and `XDP_ACTIVATE_TX_QUEUE` now return `NTSTATUS` instead
  of `VOID`.

### Removed

- (Native drivers) Remove `XdpReceive` single-frame inspection API. Use the
  batched receive API instead.
- (Native drivers) Remove `XDP_RX_ACTION_PEND`.
