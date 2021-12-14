//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

//
// This module configures XDP programs on interfaces.
//

#include "precomp.h"
#include <netiodef.h>
#include <xdpudp.h>
#include "program.tmh"

typedef struct _XDP_PROGRAM_FRAME_STORAGE {
    ETHERNET_HEADER EthHdr;
    union {
        IPV4_HEADER Ip4Hdr;
        IPV6_HEADER Ip6Hdr;
    };
    UDP_HDR UdpHdr;
} XDP_PROGRAM_FRAME_STORAGE;

typedef struct _XDP_PROGRAM_FRAME_CACHE {
    union {
        struct {
            UINT32 EthCached : 1;
            UINT32 EthValid : 1;
            UINT32 Ip4Cached : 1;
            UINT32 Ip4Valid : 1;
            UINT32 Ip6Cached : 1;
            UINT32 Ip6Valid : 1;
            UINT32 UdpCached : 1;
            UINT32 UdpValid : 1;
        };
        UINT32 Flags;
    };

    ETHERNET_HEADER *EthHdr;
    union {
        IPV4_HEADER *Ip4Hdr;
        IPV6_HEADER *Ip6Hdr;
    };
    UDP_HDR *UdpHdr;
    UINT16 AvailableUdpPayloadLength; // Amount available in the current packet or fragment.
} XDP_PROGRAM_FRAME_CACHE;

typedef struct _XDP_PROGRAM {
    //
    // Storage for discontiguous headers.
    //
    XDP_PROGRAM_FRAME_STORAGE FrameStorage;

    DECLSPEC_CACHEALIGN
    UINT32 RuleCount;
    XDP_RULE Rules[0];
} XDP_PROGRAM;

//
// Data path routines.
//

static
VOID
XdpInitializeFrameCache(
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache
    )
{
    Cache->Flags = 0;
}

static
_Success_(return != FALSE)
BOOLEAN
XdpGetContiguousHeader(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _In_ VOID *HeaderStorage,
    _In_ UINT32 HeaderSize,
    _Out_ VOID **Header
    )
{
    UCHAR *Hdr = HeaderStorage;

    UNREFERENCED_PARAMETER(Frame);

    while (HeaderSize > 0) {
        UINT32 CopyLength = min(HeaderSize, (*Buffer)->DataLength - *BufferDataOffset);
        UCHAR *Va = XdpGetVirtualAddressExtension(*Buffer, VirtualAddressExtension)->VirtualAddress;

        //
        // If the current buffer is depleted, advance to the next fragment.
        //
        if (CopyLength == 0) {
            if (*FragmentsRemaining == 0) {
                return FALSE;
            } else {
                *FragmentIndex = (*FragmentIndex + 1) & FragmentRing->Mask;
                *FragmentsRemaining -= 1;
                *Buffer = XdpRingGetElement(FragmentRing, *FragmentIndex);
                *BufferDataOffset = 0;
                continue;
            }
        }

        //
        // If the buffer contains the contiguous header, return it directly.
        //
        if (Hdr == HeaderStorage && *BufferDataOffset + HeaderSize <= (*Buffer)->DataLength) {
            *Header = Va + (*Buffer)->DataOffset + *BufferDataOffset;
            *BufferDataOffset += HeaderSize;
            return TRUE;
        }

        RtlCopyMemory(Hdr, Va + (*Buffer)->DataOffset + *BufferDataOffset, CopyLength);
        *BufferDataOffset += CopyLength;
        Hdr += CopyLength;
        HeaderSize -= CopyLength;
    }

    *Header = HeaderStorage;
    return TRUE;
}

static
VOID
XdpParseFragmentedEthernet(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->EthValid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->EthHdr, sizeof(Storage->EthHdr), &Cache->EthHdr);
}

static
VOID
XdpParseFragmentedIp4(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->Ip4Valid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->Ip4Hdr, sizeof(Storage->Ip4Hdr), &Cache->Ip4Hdr);
}

static
VOID
XdpParseFragmentedIp6(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->Ip6Valid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->Ip6Hdr, sizeof(Storage->Ip6Hdr), &Cache->Ip6Hdr);
}

static
VOID
XdpParseFragmentedUdp(
    _In_ XDP_FRAME *Frame,
    _Inout_ XDP_BUFFER **Buffer,
    _Inout_ UINT32 *BufferDataOffset,
    _Inout_ UINT32 *FragmentIndex,
    _Inout_ UINT32 *FragmentsRemaining,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    Cache->UdpValid =
        XdpGetContiguousHeader(
            Frame, Buffer, BufferDataOffset, FragmentIndex, FragmentsRemaining, FragmentRing,
            VirtualAddressExtension, &Storage->UdpHdr, sizeof(Storage->UdpHdr), &Cache->UdpHdr);
}

static
VOID
XdpParseFragmentedFrame(
    _In_ XDP_FRAME *Frame,
    _In_ XDP_RING *FragmentRing,
    _In_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Inout_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    XDP_BUFFER *Buffer = &Frame->Buffer;
    UINT32 BufferDataOffset = 0;
    UINT32 FragmentCount;
    IPPROTO IpProto = IPPROTO_MAX;

    //
    // The first buffer is stored in the frame ring, so bias the fragment index
    // so the initial increment yields the first buffer in the fragment ring.
    //
    FragmentIndex--;
    FragmentCount = XdpGetFragmentExtension(Frame, FragmentExtension)->FragmentBufferCount;

    if (!Cache->EthValid) {
        XdpParseFragmentedEthernet(
            Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
            VirtualAddressExtension, Cache, Storage);

        if (!Cache->EthValid) {
            return;
        }
    }

    if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV4)) {
        if (!Cache->Ip4Valid) {
            XdpParseFragmentedIp4(
                Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
                VirtualAddressExtension, Cache, Storage);

            if (!Cache->Ip4Valid) {
                return;
            }
        }
        IpProto = Cache->Ip4Hdr->Protocol;
    } else if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV6)) {
        if (!Cache->Ip6Valid) {
            XdpParseFragmentedIp6(
                Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
                VirtualAddressExtension, Cache, Storage);

            if (!Cache->Ip6Valid) {
                return;
            }
        }
        IpProto = Cache->Ip6Hdr->NextHeader;
    } else {
        return;
    }

    if (IpProto == IPPROTO_UDP && !Cache->UdpValid) {
        XdpParseFragmentedUdp(
            Frame, &Buffer, &BufferDataOffset, &FragmentIndex, &FragmentCount, FragmentRing,
            VirtualAddressExtension, Cache, Storage);
    }
}

static
VOID
XdpParseFrame(
    _In_ XDP_FRAME *Frame,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension,
    _Out_ XDP_PROGRAM_FRAME_CACHE *Cache,
    _Inout_ XDP_PROGRAM_FRAME_STORAGE *Storage
    )
{
    XDP_BUFFER *Buffer;
    UCHAR *Va;
    IPPROTO IpProto = IPPROTO_MAX;
    UINT32 Offset = 0;

    //
    // This routine always attempts to parse Ethernet through UDP headers.
    //
    Cache->EthCached = TRUE;
    Cache->Ip4Cached = TRUE;
    Cache->Ip6Cached = TRUE;
    Cache->UdpCached = TRUE;

    //
    // Attempt to parse all headers in a single pass over the first buffer. If
    // there's not enough data in the first buffer, fall back to handling
    // discontiguous headers via the fragment ring.
    //

    Buffer = &Frame->Buffer;
    Va = XdpGetVirtualAddressExtension(Buffer, VirtualAddressExtension)->VirtualAddress;
    Va += Buffer->DataOffset;

    if (Buffer->DataLength < sizeof(*Cache->EthHdr)) {
        goto BufferTooSmall;
    }
    Cache->EthHdr = (ETHERNET_HEADER *)&Va[Offset];
    Cache->EthValid = TRUE;
    Offset += sizeof(*Cache->EthHdr);

    if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV4)) {
        if (Buffer->DataLength < Offset + sizeof(*Cache->Ip4Hdr)) {
            goto BufferTooSmall;
        }
        Cache->Ip4Hdr = (IPV4_HEADER *)&Va[Offset];
        Cache->Ip4Valid = TRUE;
        Offset += sizeof(*Cache->Ip4Hdr);
        IpProto = Cache->Ip4Hdr->Protocol;
    } else if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV6)) {
        if (Buffer->DataLength < Offset + sizeof(*Cache->Ip6Hdr)) {
            goto BufferTooSmall;
        }
        Cache->Ip6Hdr = (IPV6_HEADER *)&Va[Offset];
        Cache->Ip6Valid = TRUE;
        Offset += sizeof(*Cache->Ip6Hdr);
        IpProto = Cache->Ip6Hdr->NextHeader;
    } else {
        return;
    }

    if (IpProto == IPPROTO_UDP) {
        if (Buffer->DataLength < Offset + sizeof(*Cache->UdpHdr)) {
            goto BufferTooSmall;
        }
        Cache->UdpHdr = (UDP_HDR *)&Va[Offset];
        Cache->UdpValid = TRUE;
        Offset += sizeof(*Cache->UdpHdr);
        Cache->AvailableUdpPayloadLength = (UINT16)(Buffer->DataLength - Offset);
    }

    return;

BufferTooSmall:

    if (FragmentRing != NULL) {
        ASSERT(FragmentExtension);
        XdpParseFragmentedFrame(
            Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
            Cache, Storage);
    }
}

static
BOOLEAN
Ipv4PrefixMatch(
    _In_ CONST IN_ADDR *Ip,
    _In_ CONST IN_ADDR *Prefix,
    _In_ CONST IN_ADDR *Mask
    )
{
    return (Ip->s_addr & Mask->s_addr) == Prefix->s_addr;
}

static
BOOLEAN
Ipv6PrefixMatch(
    _In_ CONST IN6_ADDR *Ip,
    _In_ CONST IN6_ADDR *Prefix,
    _In_ CONST IN6_ADDR *Mask
    )
{
    CONST UINT64 *Ip64 = (CONST UINT64 *)Ip;
    CONST UINT64 *Prefix64 = (CONST UINT64 *)Prefix;
    CONST UINT64 *Mask64 = (CONST UINT64 *)Mask;

    return
        ((Ip64[0] & Mask64[0]) == Prefix64[0]) &
        ((Ip64[1] & Mask64[1]) == Prefix64[1]);
}

static
BOOLEAN
UdpTupleMatch(
    _In_ XDP_MATCH_TYPE Type,
    _In_ CONST XDP_PROGRAM_FRAME_CACHE *Cache,
    _In_ CONST XDP_TUPLE *Tuple
    )
{
    if (Cache->EthHdr->Type == RtlUshortByteSwap(ETHERNET_TYPE_IPV4)) {
        return
            Type == XDP_MATCH_IPV4_UDP_TUPLE &&
            Cache->UdpHdr->uh_sport == Tuple->SourcePort &&
            Cache->UdpHdr->uh_dport == Tuple->DestinationPort &&
            IN4_ADDR_EQUAL(&Cache->Ip4Hdr->SourceAddress, &Tuple->SourceAddress.Ipv4) &&
            IN4_ADDR_EQUAL(&Cache->Ip4Hdr->DestinationAddress, &Tuple->DestinationAddress.Ipv4);
    } else { // IPv6
        return
            Type == XDP_MATCH_IPV6_UDP_TUPLE &&
            Cache->UdpHdr->uh_sport == Tuple->SourcePort &&
            Cache->UdpHdr->uh_dport == Tuple->DestinationPort &&
            IN6_ADDR_EQUAL(&Cache->Ip6Hdr->SourceAddress, &Tuple->SourceAddress.Ipv6) &&
            IN6_ADDR_EQUAL(&Cache->Ip6Hdr->DestinationAddress, &Tuple->DestinationAddress.Ipv6);
    }
}

#pragma pack(push)
#pragma pack(1)
typedef struct QUIC_HEADER_INVARIANT {
    union {
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
        };
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
            UINT32 Version;
            UCHAR DestCidLength;
            UCHAR DestCid[0];
        } LONG_HDR;
        struct {
            UCHAR VARIANT : 7;
            UCHAR IsLongHeader : 1;
            UCHAR DestCid[0];
        } SHORT_HDR;
    };
} QUIC_HEADER_INVARIANT;
#pragma pack(pop)

static
BOOLEAN
QuicCidMatch(
    _In_reads_(Length) CONST UCHAR* Payload,
    _In_ UINT16 Length,
    _In_ CONST XDP_QUIC_FLOW *Flow
    )
{
    CONST QUIC_HEADER_INVARIANT *Header = (CONST QUIC_HEADER_INVARIANT *)Payload;
    if (Length < sizeof(UCHAR)) {
        return FALSE; // Not enough room to read the IsLongHeader bit
    }

    CONST UCHAR* DestCid;
    if (Header->IsLongHeader) {
        if (Length < 6 + Flow->CidOffset + Flow->CidLength) {  // 6 is sizeof(QUIC_HEADER_INVARIANT.LONG_HDR)
            return FALSE; // Not enough room to read the CID
        }
        DestCid = Header->LONG_HDR.DestCid + Flow->CidOffset;
    } else {
        if (Length < 1 + Flow->CidOffset + Flow->CidLength) {  // 1 is sizeof(QUIC_HEADER_INVARIANT.SHORT_HDR)
            return FALSE; // Not enough room to read the CID
        }
        DestCid = Header->SHORT_HDR.DestCid + Flow->CidOffset;
    }

    return memcmp(DestCid, Flow->CidData, Flow->CidLength) == 0;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_RX_ACTION
XdpInspect(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_REDIRECT_CONTEXT *RedirectContext,
    _In_ XDP_RING *FrameRing,
    _In_ UINT32 FrameIndex,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension
    )
{
    XDP_RX_ACTION Action = XDP_RX_ACTION_PASS;
    XDP_PROGRAM_FRAME_CACHE FrameCache;
    XDP_FRAME *Frame;
    BOOLEAN Matched = FALSE;

    ASSERT(FrameIndex <= FrameRing->Mask);
    ASSERT(
        (FragmentRing == NULL && FragmentIndex == 0) ||
        (FragmentRing && FragmentIndex <= FragmentRing->Mask));

    XdpInitializeFrameCache(&FrameCache);
    Frame = XdpRingGetElement(FrameRing, FrameIndex);

    for (ULONG RuleIndex = 0; RuleIndex < Program->RuleCount; RuleIndex++) {
        XDP_RULE *Rule = &Program->Rules[RuleIndex];

        //
        // Check the match conditions.
        //

        switch (Rule->Match) {
        case XDP_MATCH_ALL:
            Matched = TRUE;
            break;

        case XDP_MATCH_UDP:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_UDP_DST:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid &&
                FrameCache.UdpHdr->uh_dport == Rule->Pattern.Port) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV4_DST_MASK:
            if (!FrameCache.Ip4Cached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip4Valid &&
                Ipv4PrefixMatch(
                    &FrameCache.Ip4Hdr->DestinationAddress, &Rule->Pattern.IpMask.Address.Ipv4,
                    &Rule->Pattern.IpMask.Mask.Ipv4)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV6_DST_MASK:
            if (!FrameCache.Ip6Cached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.Ip6Valid &&
                Ipv6PrefixMatch(
                    &FrameCache.Ip6Hdr->DestinationAddress,
                    &Rule->Pattern.IpMask.Address.Ipv6,
                    &Rule->Pattern.IpMask.Mask.Ipv6)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_QUIC_FLOW:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid &&
                FrameCache.UdpHdr->uh_dport == Rule->Pattern.QuicFlow.UdpPort &&
                QuicCidMatch(
                    (CONST UCHAR*)(FrameCache.UdpHdr + 1),
                    FrameCache.AvailableUdpPayloadLength,
                    &Rule->Pattern.QuicFlow)) {
                Matched = TRUE;
            }
            break;

        case XDP_MATCH_IPV4_UDP_TUPLE:
        case XDP_MATCH_IPV6_UDP_TUPLE:
            if (!FrameCache.UdpCached) {
                XdpParseFrame(
                    Frame, FragmentRing, FragmentExtension, FragmentIndex, VirtualAddressExtension,
                    &FrameCache, &Program->FrameStorage);
            }
            if (FrameCache.UdpValid &&
                UdpTupleMatch(
                    Rule->Match,
                    &FrameCache,
                    &Rule->Pattern.Tuple)) {
                Matched = TRUE;
            }
            break;

        default:
            ASSERT(FALSE);
            break;
        }

        if (Matched) {
            //
            // Apply the action.
            //
            switch (Rule->Action) {

            case XDP_PROGRAM_ACTION_REDIRECT:
                XdpRedirect(
                    RedirectContext, FrameIndex, FragmentIndex, Rule->Redirect.TargetType,
                    Rule->Redirect.Target);

                Action = XDP_RX_ACTION_PEND;
                break;

            case XDP_PROGRAM_ACTION_DROP:
                Action = XDP_RX_ACTION_DROP;
                break;

            case XDP_PROGRAM_ACTION_PASS:
                Action = XDP_RX_ACTION_PASS;
                break;

            default:
                ASSERT(FALSE);
                break;
            }

            break;
        }
    }

    return Action;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID *
XdpProgramGetXskBypassTarget(
    _In_ XDP_PROGRAM *Program
    )
{
    ASSERT(XdpProgramCanXskBypass(Program));
    return Program->Rules[0].Redirect.Target;
}

//
// Control path routines.
//

typedef struct _XDP_PROGRAM_OBJECT {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_BINDING_HANDLE IfHandle;
    XDP_RX_QUEUE *RxQueue;
    XDP_RX_QUEUE_NOTIFICATION_ENTRY RxQueueNotificationEntry;

    XDP_PROGRAM Program;
} XDP_PROGRAM_OBJECT;

typedef struct _XDP_PROGRAM_WORKITEM {
    XDP_BINDING_WORKITEM Bind;
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
    XDP_PROGRAM_OBJECT *ProgramObject;

    KEVENT CompletionEvent;
    NTSTATUS CompletionStatus;
} XDP_PROGRAM_WORKITEM;

static XDP_FILE_IRP_ROUTINE XdpIrpProgramClose;
static XDP_FILE_DISPATCH XdpProgramFileDispatch = {
    .Close = XdpIrpProgramClose,
};

static
VOID
XdpProgramDetachRxQueue(
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    XDP_RX_QUEUE *RxQueue = ProgramObject->RxQueue;

    ASSERT(RxQueue != NULL);

    if (XdpRxQueueGetProgram(RxQueue) == &ProgramObject->Program) {
        XdpRxQueueDeregisterNotifications(RxQueue, &ProgramObject->RxQueueNotificationEntry);
        XdpRxQueueSetProgram(RxQueue, NULL);
    }
}

static
VOID
XdpProgramDelete(
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    //
    // Detach the XDP program from the RX queue.
    //
    if (ProgramObject->RxQueue != NULL) {
        XdpProgramDetachRxQueue(ProgramObject);
        XdpRxQueueDereference(ProgramObject->RxQueue);
    }

    //
    // Clean up the XDP program after data path references are dropped.
    //

    for (ULONG Index = 0; Index < ProgramObject->Program.RuleCount; Index++) {
        XDP_RULE *Rule = &ProgramObject->Program.Rules[Index];

        if (Rule->Action == XDP_PROGRAM_ACTION_REDIRECT) {

            switch (Rule->Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                XskDereferenceDatapathHandle(Rule->Redirect.Target);
                break;

            default:
                ASSERT(FALSE);
            }
        }
    }

    ExFreePoolWithTag(ProgramObject, XDP_POOLTAG_PROGRAM);
}

static
VOID
XdpProgramRxQueueNotify(
    XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    XDP_RX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject =
        CONTAINING_RECORD(NotificationEntry, XDP_PROGRAM_OBJECT, RxQueueNotificationEntry);

    switch (NotificationType) {

    case XDP_RX_QUEUE_NOTIFICATION_DELETE:
        XdpProgramDetachRxQueue(ProgramObject);
        break;

    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN
XdpProgramCanXskBypass(
    _In_ XDP_PROGRAM *Program
    )
{
    return
        Program->RuleCount == 1 &&
        Program->Rules[0].Match == XDP_MATCH_ALL &&
        Program->Rules[0].Action == XDP_PROGRAM_ACTION_REDIRECT &&
        Program->Rules[0].Redirect.TargetType == XDP_REDIRECT_TARGET_TYPE_XSK;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpCaptureProgram(
    _In_ CONST XDP_RULE *Rules,
    _In_ ULONG RuleCount,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Out_ XDP_PROGRAM_OBJECT **NewProgramObject
    )
{
    NTSTATUS Status;
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    XDP_PROGRAM *Program = NULL;
    SIZE_T AllocationSize;

    Status = RtlSizeTMult(sizeof(*Rules), RuleCount, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }
    Status = RtlSizeTAdd(sizeof(*ProgramObject), AllocationSize, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    ProgramObject = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM);
    if (ProgramObject == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    Program = &ProgramObject->Program;

    try {
        if (RequestorMode != KernelMode) {
            ProbeForRead((VOID*)Rules, sizeof(*Rules) * RuleCount, PROBE_ALIGNMENT(XDP_RULE));
        }
        RtlCopyMemory(ProgramObject->Program.Rules, Rules, sizeof(*Rules) * RuleCount);
    } except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    for (ULONG Index = 0; Index < RuleCount; Index++) {
        XDP_RULE *Rule = &Program->Rules[Index];

        if (Rule->Match < XDP_MATCH_ALL || Rule->Match >= XDP_MATCH_COUNT) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        if (Rule->Action < XDP_PROGRAM_ACTION_DROP ||
            Rule->Action > XDP_PROGRAM_ACTION_REDIRECT) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        //
        // Capture object handle references in the context of the calling thread.
        // The handle will be validated further on the control path.
        //

        if (Rule->Action == XDP_PROGRAM_ACTION_REDIRECT) {

            switch (Rule->Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                Status =
                    XskReferenceDatapathHandle(
                        RequestorMode, &Rule->Redirect.Target, TRUE,
                        &Rule->Redirect.Target);
                break;

            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (!NT_SUCCESS(Status)) {
                goto Exit;
            }
        }

        //
        // Increment the count of validated rules. The error path will not
        // attempt to clean up unvalidated rules.
        //
        Program->RuleCount++;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (NT_SUCCESS(Status)) {
        ASSERT(ProgramObject != NULL);
        *NewProgramObject = ProgramObject;
    } else {
        if (ProgramObject != NULL) {
            XdpProgramDelete(ProgramObject);
        }
    }

    return Status;
}

static
VOID
XdpProgramAttach(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_PROGRAM_WORKITEM *Item = (XDP_PROGRAM_WORKITEM *)WorkItem;
    XDP_PROGRAM_OBJECT *ProgramObject = Item->ProgramObject;
    XDP_PROGRAM *Program = &ProgramObject->Program;
    NTSTATUS Status;

    if (Item->HookId.SubLayer != XDP_HOOK_INSPECT) {
        //
        // Only RX queue programs are currently supported.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status =
        XdpRxQueueFindOrCreate(
            Item->Bind.BindingHandle, &Item->HookId, Item->QueueId, &ProgramObject->RxQueue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (XdpRxQueueGetProgram(ProgramObject->RxQueue) != NULL) {
        Status = STATUS_DUPLICATE_OBJECTID;
        goto Exit;
    }

    //
    // Perform further rule validation that require the interface work queue.
    //
    for (ULONG Index = 0; Index < Program->RuleCount; Index++) {
        XDP_RULE *Rule = &Program->Rules[Index];

        if (Rule->Action == XDP_PROGRAM_ACTION_REDIRECT) {

            switch (Rule->Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                Status = XskValidateDatapathHandle(Rule->Redirect.Target, ProgramObject->RxQueue);
                if (!NT_SUCCESS(Status)) {
                    goto Exit;
                }

                break;

            default:
                break;
            }
        }
    }

    Status = XdpRxQueueSetProgram(ProgramObject->RxQueue, Program);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Register for interface/queue removal notifications.
    //
    XdpRxQueueRegisterNotifications(
        ProgramObject->RxQueue, &ProgramObject->RxQueueNotificationEntry, XdpProgramRxQueueNotify);

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpProgramDelete(ProgramObject);
    }

    Item->CompletionStatus = Status;
    KeSetEvent(&Item->CompletionEvent, 0, FALSE);
}

static
VOID
XdpProgramDetach(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_PROGRAM_WORKITEM *Item = (XDP_PROGRAM_WORKITEM *)WorkItem;

    XdpIfDereferenceBinding(Item->ProgramObject->IfHandle);
    XdpProgramDelete(Item->ProgramObject);

    Item->CompletionStatus = STATUS_SUCCESS;
    KeSetEvent(&Item->CompletionEvent, 0, FALSE);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpCreateProgram(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    CONST XDP_PROGRAM_OPEN *Params = NULL;
    XDP_INTERFACE_MODE InterfaceMode;
    XDP_INTERFACE_MODE *RequiredMode = NULL;
    XDP_BINDING_HANDLE BindingHandle = NULL;
    XDP_PROGRAM_WORKITEM WorkItem = {0};
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    NTSTATUS Status;

    if (Disposition != FILE_CREATE || InputBufferLength < sizeof(*Params)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    Params = InputBuffer;

    TraceEnter(
        TRACE_CORE,
        "IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Flags=%x",
        Params->IfIndex, Params->HookId.Layer, Params->HookId.Direction, Params->HookId.SubLayer,
        Params->QueueId, Params->Flags);

    if (Params->Flags & ~(XDP_ATTACH_GENERIC | XDP_ATTACH_NATIVE) ||
        !RTL_IS_CLEAR_OR_SINGLE_FLAG(Params->Flags, XDP_ATTACH_GENERIC | XDP_ATTACH_NATIVE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (Params->Flags & XDP_ATTACH_GENERIC) {
        InterfaceMode = XDP_INTERFACE_MODE_GENERIC;
        RequiredMode = &InterfaceMode;
    }

    if (Params->Flags & XDP_ATTACH_NATIVE) {
        InterfaceMode = XDP_INTERFACE_MODE_NATIVE;
        RequiredMode = &InterfaceMode;
    }

    BindingHandle = XdpIfFindAndReferenceBinding(Params->IfIndex, &Params->HookId, 1, RequiredMode);
    if (BindingHandle == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Status = XdpCaptureProgram(Params->Rules, Params->RuleCount, Irp->RequestorMode, &ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
    WorkItem.QueueId = Params->QueueId;
    WorkItem.HookId = Params->HookId;
    WorkItem.ProgramObject = ProgramObject;
    WorkItem.Bind.BindingHandle = BindingHandle;
    WorkItem.Bind.WorkRoutine = XdpProgramAttach;

    //
    // Attach the program using the inteface's work queue.
    //
    XdpIfQueueWorkItem(&WorkItem.Bind);
    KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);

    Status = WorkItem.CompletionStatus;

Exit:

    if (NT_SUCCESS(Status)) {
        ProgramObject->Header.ObjectType = XDP_OBJECT_TYPE_PROGRAM;
        ProgramObject->Header.Dispatch = &XdpProgramFileDispatch;
        ProgramObject->IfHandle = BindingHandle, BindingHandle = NULL;
        IrpSp->FileObject->FsContext = ProgramObject;
    }

    if (BindingHandle != NULL) {
        XdpIfDereferenceBinding(BindingHandle);
    }

    if (Params != NULL) {
        TraceInfo(
            TRACE_CORE,
            "Program=%p IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Flags=%x Status=%!STATUS!",
            ProgramObject, Params->IfIndex, Params->HookId.Layer,
            Params->HookId.Direction, Params->HookId.SubLayer, Params->QueueId,
            Params->Flags, Status);
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpProgramClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_PROGRAM_OBJECT *Program = IrpSp->FileObject->FsContext;
    XDP_PROGRAM_WORKITEM WorkItem = {0};

    UNREFERENCED_PARAMETER(Irp);

    TraceEnter(TRACE_CORE, "Program=%p", Program);
    TraceInfo(TRACE_CORE, "Closing Program=%p", Program);

    KeInitializeEvent(&WorkItem.CompletionEvent, NotificationEvent, FALSE);
    WorkItem.ProgramObject = Program;
    WorkItem.Bind.BindingHandle = Program->IfHandle;
    WorkItem.Bind.WorkRoutine = XdpProgramDetach;

    //
    // Perform the detach on the interface's work queue.
    //
    XdpIfQueueWorkItem(&WorkItem.Bind);
    KeWaitForSingleObject(&WorkItem.CompletionEvent, Executive, KernelMode, FALSE, NULL);
    ASSERT(NT_SUCCESS(WorkItem.CompletionStatus));

    TraceExitSuccess(TRACE_CORE);

    return STATUS_SUCCESS;
}
