# XDP_RULE structure

Specifies an XDP inspection program rule.

## Syntax

```C
//
// XDP program rule.
//
typedef struct _XDP_RULE {
    XDP_MATCH_TYPE Match;
    XDP_MATCH_PATTERN Pattern;
    XDP_RULE_ACTION Action;
    union {
        XDP_REDIRECT_PARAMS Redirect;
        //
        // Reserved.
        //
        XDP_EBPF_PARAMS Ebpf;
    };
} XDP_RULE;
```

## Members

TODO

## Remarks

TODO

## See Also

[`XDP_MATCH_TYPE`](XDP_MATCH_TYPE.md)  
[`XDP_MATCH_PATTERN`](XDP_MATCH_TYPE.md)  
[`XDP_RULE_ACTION`](XDP_RULE_ACTION.md)  
