# Per-Object-Type Security

## Overview

XDP supports finer-grained access control by providing separate device objects
for each XDP object type. Each device has its own device class GUID and can be
configured with an independent SDDL security descriptor, allowing administrators
to grant users access to specific XDP capabilities without granting access to all
of them.

## Device Objects

Per-type device objects live under the `\Device\xdpapi` object directory.

| Object Type | Device Name | Device Class GUID | Description |
|---|---|---|---|
| Common | `\Device\xdp` | `28f93d3f-4c0a-4a7c-8ff1-96b24e19b856` | Legacy device; allows any object type |
| Program | `\Device\xdpapi\program` | `6ad95b14-2cb1-4646-ba32-bc090ab436e5` | XDP programs (rules, eBPF) |
| XSK | `\Device\xdpapi\xsk` | `0903d898-39c3-4a0f-8528-13658fb280f3` | AF_XDP sockets |
| Interface | `\Device\xdpapi\interface` | `5f1fa9af-e48e-457a-b556-88492b514662` | Interface configuration (RSS, QEO) |
| Map | `\Device\xdpapi\map` | `6bbd4cfc-90eb-4126-ba25-51d97bba239f` | XDP maps (e.g. XSKMAP) |

## Architecture

### Kernel Driver

The XDP driver creates all device objects at startup in `dispatch.c`. Each
per-type device stores an `XDP_DEVICE_EXTENSION` in its device extension that
indicates which object type it allows:

```c
typedef struct _XDP_DEVICE_EXTENSION {
    BOOLEAN IsPerTypeDevice;
    XDP_OBJECT_TYPE AllowedObjectType;
} XDP_DEVICE_EXTENSION;
```

When a user-mode application opens a handle via `NtCreateFile`, the driver
validates that the requested object type (from the `XDP_OPEN_PACKET` extended
attribute) matches the device's allowed type. The common device allows any
object type for backward compatibility.

### User-Mode API

The API headers (`xdp/details/xdpapi.h`, `xdp/details/afxdp.h`) use
`_XdpOpenObjectType()` to open the per-type device. This function:

1. Attempts to open the per-type device under `\Device\xdpapi\<type>`.
2. If the application defines `XDP_MINIMUM_MAJOR_VER` and
   `XDP_MINIMUM_MINOR_VER` with a minimum version <= 1.3, and the per-type
   device does not exist, falls back to the common `\Device\xdp` device.
   Applications targeting > 1.3 do not perform the fallback because per-type
   devices were introduced after 1.3.

### xdpcfg.exe

The `xdpcfg.exe` command-line tool supports per-type SDDL:

```
xdpcfg.exe SetDeviceSddl <SDDL>                    # Common device
xdpcfg.exe SetDeviceSddl <ObjectType> <SDDL>       # Per-type device
```

Object types: `program`, `xsk`, `interface`, `map`.

## Security Model

- **Default**: All devices use `SDDL_DEVOBJ_SYS_ALL_ADM_ALL` (SYSTEM and
  Administrators have full access).
- **Per-type override**: Administrators can set per-type SDDLs to grant
  specific users access to specific object types.
- **Common device**: The common device always allows any object type. This
  provides backward compatibility with applications that open `\Device\xdp`
  directly.
- **Restart required**: SDDL changes take effect after restarting the XDP
  driver service.

> **Caution:** Per-type device SDDLs cannot enforce stricter access than the
> common device. Because the common `\Device\xdp` device allows any object
> type, any principal with access to the common device can bypass per-type
> ACLs by opening the desired object type through it. To meaningfully
> restrict access to a per-type device, the common device's SDDL must be at
> least as restrictive as the per-type SDDL.
