XDP binding requirements:

-   Exchange dispatch tables within the kernel trust boundary.
-   Compatibility with the inspected control path: filter drivers should be able
    to inspect, block, and modify bindings.

The NMR binding protocol for XDP is as follows:

1.  The interface driver registers an NMR client when an interface is created.
    The NMR client is uniquely identified by a key (GUID, InterfaceIndex) to
    ensure XDP bindings do not cross interface stack boundaries.

    XDP provides helper functions to manage this NMR client.

2.  The interface driver provides its unique key via some mechanism. This key
    does not need to be a secret nor tamper-resistant.

    For NDIS6 drivers, XDP uses an OID to query the key, which allows LWFs to
    disable XDP (by failing the OID) or to intercept XDP bindings (by returning
    their own unique key).

3.  XDP binds to an interface by registering an NMR provider identified by the
    same unique key. Once bound to the NMR client, XDP uses the interface's
    dispatch table to open, close, and modify the interface. Each NMR client and
    provider is restricted to a single binding.

4.  The interface driver may deregister its NMR client at any time; if the NMR
    client begins to detach from XDP, XDP releases any resources, closes the
    interface, and completes the NMR detach.
