# XDP_HOOK_ID structure

Specifies an XDP hook point.

## Syntax

```C
typedef enum _XDP_HOOK_LAYER {
    //
    // The XDP hook is inserted above the L2 device and below the L2+ protocol
    // stack.
    //
    //     +-------------------------+
    //     |      L2+ Protocols      |
    //     +------+------------------+
    //            |           ^
    //            v           |
    //  {  +-----------+ +----+------+   }
    // ={  | XDP L2 TX | | XDP L2 RX |   }= XDP_HOOK_L2
    //  {  +------+----+ +-----------+   }
    //            |           ^
    //            v           |
    //     +------------------+------+
    //     |       L2 Interface      |
    //     +-------------------------+
    //
    // Supported hook directions:
    //
    //  XDP_HOOK_RX:    The hook is inserted in the receive data path, after
    //                  the L2 interface has received a frame and before the
    //                  frame is indicated to the L2+ protocols.
    //
    //  XDP_HOOK_TX:    The hook is inserted in the transmit data path, after
    //                  the L2+ protocols have transmitted a frame and before
    //                  the frame is transmitted to the L2 interface.
    //
    XDP_HOOK_L2,
} XDP_HOOK_LAYER;

typedef enum _XDP_HOOK_DATAPATH_DIRECTION {
    //
    // XDP metadata has receive semantics. The data path hook point is dependent
    // on the hook layer.
    //
    XDP_HOOK_RX,

    //
    // XDP metadata has transmit semantics. The data path hook point is
    // dependent on the hook layer.
    //
    XDP_HOOK_TX,
} XDP_HOOK_DATAPATH_DIRECTION;

//
//       Datapath
//          |
//          v
// +------------------+
// | XDP Inspect Hook |
// +--------+---------+
//          |
//          |  +-----------------+
//          |  | XDP Inject Hook |
//          |  +--------+--------+
//          v           v
//          |<----------+
//          |
//          v
//       Datapath
//
typedef enum _XDP_HOOK_SUBLAYER {
    //
    // XDP hook inspects frames on a data path at a given layer.
    //
    XDP_HOOK_INSPECT,

    //
    // XDP hook injects frames onto a data path at a given layer.
    //
    XDP_HOOK_INJECT,
} XDP_HOOK_SUBLAYER;

typedef struct _XDP_HOOK_ID {
    XDP_HOOK_LAYER Layer;
    XDP_HOOK_DATAPATH_DIRECTION Direction;
    XDP_HOOK_SUBLAYER SubLayer;
} XDP_HOOK_ID;

C_ASSERT(
    sizeof(XDP_HOOK_ID) ==
        sizeof(XDP_HOOK_LAYER) +
        sizeof(XDP_HOOK_DATAPATH_DIRECTION) +
        sizeof(XDP_HOOK_SUBLAYER));

```

## Members

TODO

## Remarks

TODO
