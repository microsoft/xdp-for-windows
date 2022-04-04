# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Changed

- (Native drivers) Rename `XDP_RING::NextIndex` to `XDP_RING::InterfaceReserved`

### Removed

- (Native drivers) Remove `XdpReceive` single-frame inspection API. Use the
  batched receive API instead.
- (Native drivers) Remove `XDP_RX_ACTION_PEND`.
