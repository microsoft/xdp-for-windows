# XDP Miniport

XDPMP is an NDIS miniport that implements the native XDP interface.

Native XDP test miniport dependencies:

- bin\idw\dswdevice.exe
- test\xdpmp\\*
- fakendis\fndis.sys (experimental NDIS polling API)

To install the XDPMP miniport driver:

```PowerShell
bcdedit.exe /set bootdebug on
copy .\test\fakendis\fndis.sys c:\windows\system32\drivers\fndis.sys
sc.exe create fndis type= kernel start= boot binPath= c:\windows\system32\drivers\fndis.sys
shutdown /r /f /t 0
cd test\xdpmp
.\xdpmp.ps1 -Install
```

## Configuration

XDPMP optional features are configurable using NetAdapter advanced properties
and/or registry keys.

Additionally, XDPMP supports a load generator and rate limiter. RX load
generation and TX rate limiting  can be dynamically configured with
`xdpmppace.ps1`.
