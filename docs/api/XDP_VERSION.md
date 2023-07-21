# XDP_HOOK_ID structure

Specifies an XDP driver API version.

## Syntax

```C
typedef struct _XDP_VERSION {
    //
    // A change to the Major version number signifies a new XDP API which is not backwards
    // compatible with the previous versions of the API.
    //
    UINT32 Major;

    //
    // A change to the Minor version number signifies new features added to the XDP API
    // which are backwards compatible.
    //
    UINT32 Minor;

    //
    // A change to the Patch number signifies bug fixes for the XDP platform
    // which are backwards compatible.
    //
    UINT32 Patch;
} XDP_VERSION;

```

## Members

TODO

## Remarks

TODO
