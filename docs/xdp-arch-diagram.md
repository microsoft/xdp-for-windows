````mermaid
flowchart TB
%% =========================
%% USER MODE APP
%% =========================
subgraph USER["User mode app"]
S1["AF_XDP socket"]
S2["AF_XDP socket"]
S3["AF_XDP socket"]
S4["AF_XDP socket"]
S5["AF_XDP socket"]
S6["AF_XDP socket"]
S7["AF_XDP socket"]
end

    %% =========================
    %% XSK LAYER
    %% =========================
    subgraph XSK["XSK Bindings"]
        RX1["RX Bound XSK<br/>[activated]"]
        RX2["RX Bound XSK<br/>[activated]"]
        RX3["RX Bound XSK<br/>[not activated]"]

        UNB["Unbound XSK<br/>[not activated]"]

        TX1["TX Bound XSK<br/>[activated]"]
        TX2["TX Bound XSK<br/>[activated]"]
        TX3["TX Bound XSK<br/>[not activated]"]
    end

    S1 --> RX1
    S2 --> RX2
    S3 --> RX3
    S4 --> UNB
    S5 --> TX1
    S6 --> TX2
    S7 --> TX3

    %% =========================
    %% XDP CORE
    %% =========================
    subgraph CORE["XDP Core"]
        RXQ1["XDP_RX_QUEUE1"]
        RXQ2["XDP_RX_QUEUE2"]

        TXQ1["XDP_TX_QUEUE1"]
        TXQ2["XDP_TX_QUEUE2"]

        PROG["Program Attached<br/>(xdp rules / ebpf)"]
        NOPROG["NO program attached yet"]
    end

    RX1 --> RXQ1
    RX2 --> RXQ1
    RX3 --> RXQ2

    TX1 --> TXQ1
    TX2 --> TXQ1
    TX3 --> TXQ2

    RXQ1 --> PROG
    RXQ2 --> NOPROG

    %% =========================
    %% XDP LWF
    %% =========================
    subgraph LWF["XDP LWF"]
        RXGEN1["RX_LWF_GENERIC1"]

        subgraph RXNOTIFY["RX Notification Queue"]
            RXN2["RX_LWF_NOTIFY2"]
            RXN1["RX_LWF_NOTIFY1"]
        end

        subgraph TXNOTIFY["TX Notification Queue"]
            TXN2["TX_LWF_GENERIC2<br/>TX_LWF_NOTIFY"]
            TXN1["TX_LWF_GENERIC1<br/>TX_LWF_NOTIFY"]
        end
    end

    PROG --> RXGEN1
    RXQ2 --> RXN2
    RXQ1 --> RXN1

    TXQ1 --> TXN2
    TXQ2 --> TXN1

    %% =========================
    %% DRIVER LAYER
    %% =========================
    subgraph DRIVER["NDIS / miniport driver"]
        DRV["Network Driver"]
    end

    RXGEN1 --> DRV
    RXN1 --> DRV
    TXN1 --> DRV
    TXN2 --> DRV

    %% =========================
    %% STYLING
    %% =========================
    classDef socket fill:#0b5d8a,color:#fff,stroke:#1ea7ff;
    classDef queue fill:#135d7a,color:#fff,stroke:#1ea7ff;
    classDef notify fill:#1f6d8c,color:#fff,stroke:#1ea7ff;
    classDef inactive fill:#555,color:#fff,stroke:#999;
    classDef prog fill:#0d4f66,color:#fff,stroke:#1ea7ff;

    class S1,S2,S3,S4,S5,S6,S7 socket;
    class RX1,RX2,RX3,TX1,TX2,TX3,UNB queue;
    class RXQ1,RXQ2,TXQ1,TXQ2 queue;
    class RXGEN1,RXN1,RXN2,TXN1,TXN2 notify;
    class PROG,NOPROG prog;
```
