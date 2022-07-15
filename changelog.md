# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- rxfilter sample application

## [0.16.2] 2022-05-19

### Added

- (AF_XDP) Ideal processor affinity query socket options.

### Changed
- (AF_XDP) Remove `sharedUmemSock` parameter from `XskActivate` and replace with
  the `XSK_SOCKOPT_SHARE_UMEM` socket option.

## [0.16.1] - 2022-05-02

### Added

- (AF_XDP) Add XskActivate API to enable AF_XDP transmit and receive data paths. This routine
  requires the socket is already bound to an XDP queue using XskBind.
- (AF_XDP) Add RSS capabilities API.

### Changed

- (AF_XDP) The XskBind API no longer requires descriptor rings be configured and no longer activates
  the data path. Instead, applications indicate whether RX and/or TX should be bound using flags.
- (AF_XDP) Renamed `XdpRssOpen` to `XdpInterfaceOpen`
- (AF_XDP) Obligate applications to invoke `XskNotifySocket` after dequeuing elements from the TX
  completion ring when elements are already on the TX ring, and rely on the updated
  `XSK_RING_FLAG_NEED_POKE` flag to elide unnecessary system calls.

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
