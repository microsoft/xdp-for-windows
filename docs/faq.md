# Frequently Asked Questions

> Is this a fork of the Linux XDP project?

No. We've been heavily inspired and influenced by the Linux XDP project, but we don't reuse any
Linux code.

> Do you support AF_XDP?

Yes, we provide an AF_XDP [API](/published/external/afxdp.h) sockets API. This API does not rely on
Winsock; it is an independent socket API. It is not directly source-compatible with
[the Linux API](https://www.kernel.org/doc/html/latest/networking/af_xdp.html), though it is
possible to build a thin shim layer to abstract any differences.

> Do you support eBPF programs?

Not yet. We [plan](https://github.com/microsoft/xdp-for-windows/issues/7) to integrate with the
[eBPF for Windows project](https://github.com/microsoft/ebpf-for-windows) soon. In the meantime, we
have built a barebones [program](/published/external/xdp/program.h) module.

> What versions of Windows does XDP support?

XDP is currently tested on Windows Server 2019 and 2022.

> When is this shipping with Windows?

At this time, there is no plan to ship XDP as a part of Windows. XDP will release separately from
Windows, likely directly through GitHub.
