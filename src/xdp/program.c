//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This module configures XDP programs on interfaces.
//

#include "precomp.h"
#include "ebpfstore.h"
#include "programinspect.h"

typedef struct _EBPF_PROG_TEST_RUN_CONTEXT {
    char* Data;
    SIZE_T DataSize;
} EBPF_PROG_TEST_RUN_CONTEXT;

typedef struct _EBPF_XDP_MD {
    EBPF_CONTEXT_HEADER;
    xdp_md_t Base;
    EBPF_PROG_TEST_RUN_CONTEXT* ProgTestRunContext;
} EBPF_XDP_MD;

static __forceinline NTSTATUS EbpfResultToNtStatus(ebpf_result_t Result)
{
    switch (Result) {
    case EBPF_SUCCESS:
        return STATUS_SUCCESS;
    case EBPF_INVALID_ARGUMENT:
        return STATUS_INVALID_PARAMETER;
    case EBPF_NO_MEMORY:
        return STATUS_NO_MEMORY;
    default:
        return STATUS_UNSUCCESSFUL;
    }
}

//
// Routines for BPF_PROG_TEST_RUN.
//

static void EbpfProgramTestRunContextFree(_In_opt_ _Post_invalid_ EBPF_PROG_TEST_RUN_CONTEXT* Context)
{
    if (Context == NULL) {
        return;
    }

    if (Context->Data != NULL) {
        ExFreePool(Context->Data);
    }
    ExFreePool(Context);
}

/**
 * @brief Build a EBPF_XDP_MD Context for the eBPF program. This includes copying the packet data and
 * metadata into a contiguous buffer and building an MDL chain for the same.
 *
 * @param[in] DataIn The packet data.
 * @param[in] DataSizeIn The size of the packet data.
 * @param[in] context_in The Context.
 * @param[in] ContextSizeIn The size of the Context.
 * @param[out] Context The Context to be passed to the eBPF program.
 * @retval STATUS_SUCCESS The operation was successful.
 * @retval STATUS_INVALID_PARAMETER One or more parameters are incorrect.
 * @retval STATUS_NO_MEMORY Failed to allocate resources for this operation.
 */
static ebpf_result_t
XdpCreateContext(
    _In_reads_bytes_opt_(DataSizeIn) const uint8_t* DataIn,
    size_t DataSizeIn,
    _In_reads_bytes_opt_(ContextSizeIn) const uint8_t* context_in,
    size_t ContextSizeIn,
    _Outptr_ void** Context)
{
    NTSTATUS Status;
    ebpf_result_t EbpfResult;
    EBPF_XDP_MD* XdpMd = NULL;

    TraceEnter(TRACE_CORE, "Create program context, DataIn=%p, DataSizeIn=%llu", DataIn, DataSizeIn);

    *Context = NULL;

    // Data is mandatory. Context is optional.
    if (DataIn == NULL || DataSizeIn == 0) {
        EbpfResult = EBPF_INVALID_ARGUMENT;
        goto Exit;
    }

    // Allocate XdpMd struct.
    XdpMd = (EBPF_XDP_MD*)ExAllocatePoolZero(NonPagedPoolNx, sizeof(EBPF_XDP_MD), XDP_POOLTAG_PROGRAM_CONTEXT);
    if (XdpMd == NULL) {
        EbpfResult = EBPF_NO_MEMORY;
        goto Exit;
    }

    // Allocate memory for ProgTestRunContext
    XdpMd->ProgTestRunContext = (EBPF_PROG_TEST_RUN_CONTEXT*)ExAllocatePoolZero(
        NonPagedPoolNx, sizeof(EBPF_PROG_TEST_RUN_CONTEXT), XDP_POOLTAG_PROGRAM_CONTEXT);
    if (XdpMd->ProgTestRunContext == NULL) {
        EbpfResult = EBPF_NO_MEMORY;
        goto Exit;
    }

    // Allocate buffer for data.
    XdpMd->ProgTestRunContext->Data = (char*)ExAllocatePoolZero(NonPagedPoolNx, DataSizeIn, XDP_POOLTAG_PROGRAM_CONTEXT);
    if (XdpMd->ProgTestRunContext->Data == NULL) {
        EbpfResult = EBPF_NO_MEMORY;
        goto Exit;
    }
    memcpy(XdpMd->ProgTestRunContext->Data, DataIn, DataSizeIn);
    XdpMd->ProgTestRunContext->DataSize = DataSizeIn;

    XdpMd->Base.data = (void*)XdpMd->ProgTestRunContext->Data;
    XdpMd->Base.data_end = (void*)(XdpMd->ProgTestRunContext->Data + XdpMd->ProgTestRunContext->DataSize);

    if (context_in != NULL && ContextSizeIn >= sizeof(xdp_md_t)) {
        xdp_md_t* xdp_context = (xdp_md_t*)context_in;
        XdpMd->Base.data_meta = xdp_context->data_meta;
        XdpMd->Base.ingress_ifindex = xdp_context->ingress_ifindex;
    }

    *Context = &XdpMd->Base;
    XdpMd = NULL;

    EbpfResult = EBPF_SUCCESS;

Exit:
    if (XdpMd != NULL) {
        EbpfProgramTestRunContextFree(XdpMd->ProgTestRunContext);
        ExFreePool(XdpMd);
    }

    Status = EbpfResultToNtStatus(EbpfResult);

    TraceExitStatus(TRACE_CORE);

    return EbpfResult;
}

static void
XdpDeleteContext(
    _In_opt_ void* Context,
    _Out_writes_bytes_to_(*DataSizeOut, *DataSizeOut) uint8_t* DataOut,
    _Inout_ size_t* DataSizeOut,
    _Out_writes_bytes_to_(*ContextSizeOut, *ContextSizeOut) uint8_t* ContextOut,
    _Inout_ size_t* ContextSizeOut)
{
    EBPF_XDP_MD* XdpMd = NULL;

    TraceEnter(TRACE_CORE, "Delete program context, Context=%p", Context);

    if (Context == NULL) {
        goto Exit;
    }

    XdpMd = CONTAINING_RECORD(Context, EBPF_XDP_MD, Base);

    // Copy the packet data to the output buffer.
    if (DataOut != NULL && DataSizeOut != NULL && XdpMd->Base.data != NULL) {
        size_t DataSize = *DataSizeOut;
        size_t XdpDataSize = (char*)(XdpMd->Base.data_end) - (char*)(XdpMd->Base.data);
        if (DataSize > XdpDataSize) {
            DataSize = XdpDataSize;
        }
        memcpy(DataOut, XdpMd->Base.data, DataSize);
        *DataSizeOut = DataSize;
    } else {
        *DataSizeOut = 0;
    }

    // Copy some fields from the Context to the output buffer.
    if (ContextOut != NULL && ContextSizeOut != NULL) {
        size_t context_size = *ContextSizeOut;
        if (context_size > sizeof(xdp_md_t)) {
            context_size = sizeof(xdp_md_t);
        }

        xdp_md_t* XdpContextOut = (xdp_md_t*)ContextOut;
        XdpContextOut->data_meta = XdpMd->Base.data_meta;
        XdpContextOut->ingress_ifindex = XdpMd->Base.ingress_ifindex;
        *ContextSizeOut = context_size;
    } else {
        *ContextSizeOut = 0;
    }

    EbpfProgramTestRunContextFree(XdpMd->ProgTestRunContext);
    ExFreePool(XdpMd);

Exit:
    TraceExitSuccess(TRACE_CORE);
}

//
// Data path routines.
//

static
XDP_RX_ACTION
XdpInvokeEbpf(
    _In_ HANDLE EbpfTarget,
    _In_ XDP_INSPECTION_CONTEXT *InspectionContext,
    _In_ XDP_FRAME *Frame,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension
    )
{
    const EBPF_EXTENSION_CLIENT *Client = (const EBPF_EXTENSION_CLIENT *)EbpfTarget;
    const VOID *ClientBindingContext = EbpfExtensionClientGetClientContext(Client);
    XDP_PCW_RX_QUEUE *RxQueueStats = XdpRxQueueGetStatsFromInspectionContext(InspectionContext);
    XDP_INSPECTION_EBPF_CONTEXT *EbpfContext = &InspectionContext->EbpfContext;
    XDP_BUFFER *Buffer;
    UCHAR *Va;
    EBPF_XDP_MD XdpMd;
    ebpf_result_t EbpfResult;
    XDP_RX_ACTION RxAction;
    UINT32 Result;

    UNREFERENCED_PARAMETER(FragmentIndex);

    ASSERT((FragmentRing == NULL) || (FragmentExtension != NULL));

    //
    // Fragmented frames require special handling for eBPF programs using direct
    // packet access. On Linux, the program must be loaded with a specific flag
    // in order to inspect discontiguous packets. On Windows, discontiguous
    // frames are always inspected by default, at least until a program flag API
    // is supported by eBPF-for-Windows.
    //
    // https://github.com/microsoft/ebpf-for-windows/issues/3576
    // https://github.com/microsoft/xdp-for-windows/issues/517
    //
    if (FragmentRing != NULL &&
        XdpGetFragmentExtension(Frame, FragmentExtension)->FragmentBufferCount != 0) {
        STAT_INC(RxQueueStats, InspectFramesDiscontiguous);
    }

    Buffer = &Frame->Buffer;
    Va = XdpGetVirtualAddressExtension(Buffer, VirtualAddressExtension)->VirtualAddress;
    Va += Buffer->DataOffset;

    XdpMd.Base.data = Va;
    XdpMd.Base.data_end = Va + Buffer->DataLength;
    XdpMd.Base.data_meta = 0;
    XdpMd.Base.ingress_ifindex = InspectionContext->IfIndex;

    ebpf_program_batch_invoke_function_t EbpfInvokeProgram =
        EbpfExtensionClientGetProgramDispatch(Client)->ebpf_program_batch_invoke_function;
    EbpfResult = EbpfInvokeProgram(ClientBindingContext, &XdpMd.Base, &Result, EbpfContext);

    if (EbpfResult != EBPF_SUCCESS) {
        EventWriteEbpfProgramFailure(&MICROSOFT_XDP_PROVIDER, ClientBindingContext, EbpfResult);
        RxAction = XDP_RX_ACTION_DROP;
        STAT_INC(RxQueueStats, InspectFramesDropped);
        goto Exit;
    }

    switch (Result) {
    case XDP_PASS:
        RxAction = XDP_RX_ACTION_PASS;
        STAT_INC(RxQueueStats, InspectFramesPassed);
        break;

    case XDP_TX:
        RxAction = XDP_RX_ACTION_TX;
        STAT_INC(RxQueueStats, InspectFramesForwarded);
        break;

    default:
        ASSERT(FALSE);
        __fallthrough;
    case XDP_DROP:
        RxAction = XDP_RX_ACTION_DROP;
        STAT_INC(RxQueueStats, InspectFramesDropped);
        break;
    }

Exit:

    return RxAction;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_RX_ACTION
XdpInspectEbpf(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_INSPECTION_CONTEXT *InspectionContext,
    _In_ XDP_RING *FrameRing,
    _In_ UINT32 FrameIndex,
    _In_opt_ XDP_RING *FragmentRing,
    _In_opt_ XDP_EXTENSION *FragmentExtension,
    _In_ UINT32 FragmentIndex,
    _In_ XDP_EXTENSION *VirtualAddressExtension
    )
{
    XDP_FRAME *Frame;

    ASSERT(XdpProgramIsEbpf(Program));
    ASSERT(FrameIndex <= FrameRing->Mask);
    ASSERT(
        (FragmentRing == NULL && FragmentIndex == 0) ||
        (FragmentRing && FragmentIndex <= FragmentRing->Mask));

    Frame = XdpRingGetElement(FrameRing, FrameIndex);

    return
        XdpInvokeEbpf(
            Program->Rules[0].Ebpf.Target, InspectionContext, Frame, FragmentRing,
            FragmentExtension, FragmentIndex, VirtualAddressExtension);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
EBPF_EXTENSION_CLIENT *
XdpGetEbpfExtensionClientFromProgram(
    _In_ XDP_PROGRAM *Program
    )
{
    ASSERT(XdpProgramIsEbpf(Program));
    return (EBPF_EXTENSION_CLIENT *)Program->Rules[0].Ebpf.Target;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Success_(return)
BOOLEAN
XdpInspectEbpfStartBatch(
    _In_ XDP_PROGRAM *Program,
    _Inout_ XDP_INSPECTION_CONTEXT *InspectionContext
    )
{
    ebpf_result_t EbpfResult;
    const EBPF_EXTENSION_CLIENT *Client = XdpGetEbpfExtensionClientFromProgram(Program);

    ebpf_program_batch_begin_invoke_function_t EbpfBatchBegin =
        EbpfExtensionClientGetProgramDispatch(Client)->ebpf_program_batch_begin_invoke_function;

    EbpfResult = EbpfBatchBegin(sizeof(InspectionContext->EbpfContext), &InspectionContext->EbpfContext);

    return EbpfResult == EBPF_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpInspectEbpfEndBatch(
    _In_ XDP_PROGRAM *Program,
    _Inout_ XDP_INSPECTION_CONTEXT *InspectionContext
    )
{
    ebpf_result_t EbpfResult;
    const EBPF_EXTENSION_CLIENT *Client = XdpGetEbpfExtensionClientFromProgram(Program);

    ebpf_program_batch_end_invoke_function_t EbpfBatchEnd =
            EbpfExtensionClientGetProgramDispatch(Client)->ebpf_program_batch_end_invoke_function;

    EbpfResult = EbpfBatchEnd(&InspectionContext->EbpfContext);

    ASSERT(EbpfResult == EBPF_SUCCESS);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID *
XdpProgramGetXskBypassTarget(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    DBG_UNREFERENCED_PARAMETER(RxQueue);

    ASSERT(XdpProgramCanXskBypass(Program, RxQueue));
    return Program->Rules[0].Redirect.Target;
}

//
// Control path routines.
//
typedef struct _XDP_PROGRAM_OBJECT XDP_PROGRAM_OBJECT;

typedef struct _XDP_PROGRAM_BINDING {
    LIST_ENTRY Link;
    XDP_RX_QUEUE *RxQueue;
    LIST_ENTRY RxQueueEntry;
    XDP_RX_QUEUE_NOTIFICATION_ENTRY RxQueueNotificationEntry;
    XDP_PROGRAM_OBJECT *OwningProgram;
} XDP_PROGRAM_BINDING;

typedef struct _XDP_PROGRAM_WORKITEM {
    XDP_BINDING_WORKITEM Bind;
    XDP_HOOK_ID HookId;
    UINT32 IfIndex;
    UINT32 QueueId;
    XDP_PROGRAM_OBJECT *ProgramObject;
    BOOLEAN BindToAllQueues;

    KEVENT *CompletionEvent;
    NTSTATUS CompletionStatus;
} XDP_PROGRAM_WORKITEM;

typedef struct _XDP_PROGRAM_OBJECT {
    XDP_FILE_OBJECT_HEADER Header;
    XDP_BINDING_HANDLE IfHandle;
    LIST_ENTRY ProgramBindings;
    ULONG_PTR CreatedByPid;
    XDP_PROGRAM_WORKITEM CloseWorkItem;

    XDP_PROGRAM Program;
} XDP_PROGRAM_OBJECT;

static XDP_FILE_IRP_ROUTINE XdpIrpProgramClose;
static XDP_FILE_DISPATCH XdpProgramFileDispatch = {
    .Close = XdpIrpProgramClose,
};

static
VOID
XdpProgramTrace(
    _In_ const XDP_PROGRAM *Program
    )
{
    for (UINT32 i = 0; i < Program->RuleCount; i++) {
        const XDP_RULE *Rule = &Program->Rules[i];

        switch (Rule->Match) {
        case XDP_MATCH_ALL:
            TraceInfo(TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_ALL", Program, i);
            break;

        case XDP_MATCH_UDP:
            TraceInfo(TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_UDP", Program, i);
            break;

        case XDP_MATCH_UDP_DST:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_UDP_DST Port=%u",
                Program, i, ntohs(Rule->Pattern.Port));
            break;

        case XDP_MATCH_IPV4_DST_MASK:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_DST_MASK Ip=%!IPADDR! Mask=%!IPADDR!",
                Program, i, Rule->Pattern.IpMask.Address.Ipv4.s_addr,
                Rule->Pattern.IpMask.Mask.Ipv4.s_addr);
            break;

        case XDP_MATCH_IPV6_DST_MASK:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_DST_MASK Ip=%!IPV6ADDR! Mask=%!IPV6ADDR!",
                Program, i, Rule->Pattern.IpMask.Address.Ipv6.u.Byte,
                Rule->Pattern.IpMask.Mask.Ipv6.u.Byte);
            break;

        case XDP_MATCH_INNER_IPV4_DST_MASK_UDP:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_INNER_IPV4_DST_MASK_UDP Ip=%!IPADDR! Mask=%!IPADDR!",
                Program, i, Rule->Pattern.IpMask.Address.Ipv4.s_addr,
                Rule->Pattern.IpMask.Mask.Ipv4.s_addr);
            break;

        case XDP_MATCH_INNER_IPV6_DST_MASK_UDP:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_INNER_IPV6_DST_MASK_UDP Ip=%!IPV6ADDR! Mask=%!IPV6ADDR!",
                Program, i, Rule->Pattern.IpMask.Address.Ipv6.u.Byte,
                Rule->Pattern.IpMask.Mask.Ipv6.u.Byte);
            break;

        case XDP_MATCH_QUIC_FLOW_SRC_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_QUIC_FLOW_SRC_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                Program, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_TCP_QUIC_FLOW_SRC_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_TCP_QUIC_FLOW_SRC_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                Program, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_QUIC_FLOW_DST_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_QUIC_FLOW_DST_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                Program, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_TCP_QUIC_FLOW_DST_CID:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_TCP_QUIC_FLOW_DST_CID "
                "Port=%u CidOffset=%u CidLength=%u CidData=%!HEXDUMP!",
                Program, i, ntohs(Rule->Pattern.QuicFlow.UdpPort),
                Rule->Pattern.QuicFlow.CidOffset, Rule->Pattern.QuicFlow.CidLength,
                WppHexDump(Rule->Pattern.QuicFlow.CidData, Rule->Pattern.QuicFlow.CidLength));
            break;

        case XDP_MATCH_IPV4_UDP_TUPLE:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_UDP_TUPLE "
                "Source=%!IPADDR!:%u Destination=%!IPADDR!:%u",
                Program, i, Rule->Pattern.Tuple.SourceAddress.Ipv4.s_addr,
                ntohs(Rule->Pattern.Tuple.SourcePort),
                Rule->Pattern.Tuple.DestinationAddress.Ipv4.s_addr,
                ntohs(Rule->Pattern.Tuple.DestinationPort));
            break;

        case XDP_MATCH_IPV6_UDP_TUPLE:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_UDP_TUPLE "
                "Source=[%!IPV6ADDR!]:%u Destination=[%!IPV6ADDR!]:%u",
                Program, i, Rule->Pattern.Tuple.SourceAddress.Ipv6.u.Byte,
                ntohs(Rule->Pattern.Tuple.SourcePort),
                Rule->Pattern.Tuple.DestinationAddress.Ipv6.u.Byte,
                ntohs(Rule->Pattern.Tuple.DestinationPort));
            break;

        case XDP_MATCH_UDP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_UDP_PORT_SET PortSet=?",
                Program, i);
            break;

        case XDP_MATCH_IPV4_UDP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_UDP_PORT_SET "
                "Destination=%!IPADDR! PortSet=?",
                Program, i, Rule->Pattern.IpPortSet.Address.Ipv4.s_addr);
            break;

        case XDP_MATCH_IPV6_UDP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_UDP_PORT_SET "
                "Destination=%!IPV6ADDR! PortSet=?",
                Program, i, Rule->Pattern.IpPortSet.Address.Ipv6.u.Byte);
            break;

        case XDP_MATCH_IPV4_TCP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV4_TCP_PORT_SET "
                "Destination=%!IPADDR! PortSet=?",
                Program, i, Rule->Pattern.IpPortSet.Address.Ipv4.s_addr);
            break;

        case XDP_MATCH_IPV6_TCP_PORT_SET:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_IPV6_TCP_PORT_SET "
                "Destination=%!IPV6ADDR! PortSet=?",
                Program, i, Rule->Pattern.IpPortSet.Address.Ipv6.u.Byte);
            break;

        case XDP_MATCH_TCP_DST:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_TCP_DST Port=%u",
                Program, i, ntohs(Rule->Pattern.Port));
            break;

        case XDP_MATCH_TCP_CONTROL_DST:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_TCP_CONTROL_DST Port=%u",
                Program, i, ntohs(Rule->Pattern.Port));
            break;

        case XDP_MATCH_IP_NEXT_HEADER:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u]=XDP_MATCH_IP_NEXT_HEADER NextHeader=%u",
                Program, i, ntohs(Rule->Pattern.NextHeader));
            break;

        case XDP_MATCH_ICMPV4_ECHO_REPLY_IP_DST:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_ICMPV4_ECHO_REPLY_IP_DST Ip=%!IPADDR!",
                Program, i, Rule->Pattern.IpMask.Address.Ipv4.s_addr);
            break;

        case XDP_MATCH_ICMPV6_ECHO_REPLY_IP_DST:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u]=XDP_MATCH_ICMPV6_ECHO_REPLY_IP_DST Ip=%!IPV6ADDR!",
                Program, i, Rule->Pattern.IpMask.Address.Ipv6.u.Byte);
            break;

        default:
            ASSERT(FALSE);
            break;
        }

        switch (Rule->Action) {
        case XDP_PROGRAM_ACTION_DROP:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_DROP",
                Program, i);
            break;

        case XDP_PROGRAM_ACTION_PASS:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_PASS",
                Program, i);
            break;

        case XDP_PROGRAM_ACTION_REDIRECT:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_REDIRECT "
                "TargetType=%!REDIRECT_TARGET_TYPE! Target=%p",
                Program, i, Rule->Redirect.TargetType, Rule->Redirect.Target);
            break;

        case XDP_PROGRAM_ACTION_L2FWD:
            TraceInfo(
                TRACE_CORE, "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_L2FWD",
                Program, i);
            break;

        case XDP_PROGRAM_ACTION_EBPF:
            TraceInfo(
                TRACE_CORE,
                "Program=%p Rule[%u] Action=XDP_PROGRAM_ACTION_EBPF "
                "Target=%p",
                Program, i, Rule->Ebpf.Target);
            break;

        default:
            ASSERT(FALSE);
            break;
        }
    }
}

static
VOID
XdpProgramTraceObject(
    _In_ const XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    TraceInfo(
        TRACE_CORE, "ProgramObject=%p Program=%p CreatedByPid=%Iu",
        ProgramObject, &ProgramObject->Program, ProgramObject->CreatedByPid);

    XdpProgramTrace(&ProgramObject->Program);
}

static
int
EbpfXdpAdjustHead(
    _Inout_ xdp_md_t *Context,
    _In_ int Delta
    )
{
    //
    // Not implemented. Any return < 0 is an error.
    //
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Delta);
    return -1;
}

static const VOID *EbpfXdpHelperFunctions[] = {
    (VOID *)EbpfXdpAdjustHead,
};

static const ebpf_helper_function_addresses_t XdpHelperFunctionAddresses = {
    .header = EBPF_HELPER_FUNCTION_ADDRESSES_HEADER,
    .helper_function_count = RTL_NUMBER_OF(EbpfXdpHelperFunctions),
    .helper_function_address = (UINT64 *)EbpfXdpHelperFunctions
};

static const ebpf_program_data_t EbpfXdpProgramData = {
    .header = EBPF_PROGRAM_DATA_HEADER,
    .program_info = &EbpfXdpProgramInfo,
    .program_type_specific_helper_function_addresses = &XdpHelperFunctionAddresses,
    .context_create = XdpCreateContext,
    .context_destroy = XdpDeleteContext,
    .required_irql = DISPATCH_LEVEL,
};

static const NPI_MODULEID EbpfXdpProgramInfoProviderModuleId = {
    .Length = sizeof(NPI_MODULEID),
    .Type = MIT_GUID,
    .Guid = EBPF_PROGRAM_TYPE_XDP_INIT,
};

static const ebpf_attach_provider_data_t EbpfXdpHookAttachProviderData = {
    .header = EBPF_ATTACH_PROVIDER_DATA_HEADER,
    .supported_program_type = EBPF_PROGRAM_TYPE_XDP_INIT,
    .bpf_attach_type = BPF_XDP,
    .link_type = BPF_LINK_TYPE_XDP,
};

static const NPI_MODULEID EbpfXdpHookProviderModuleId = {
    .Length = sizeof(NPI_MODULEID),
    .Type = MIT_GUID,
    .Guid = EBPF_ATTACH_TYPE_XDP_INIT,
};

static EBPF_EXTENSION_PROVIDER *EbpfXdpProgramInfoProvider;
static EBPF_EXTENSION_PROVIDER *EbpfXdpProgramHookProvider;

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpProgramUpdateCompiledProgram(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    LIST_ENTRY *BindingListHead = XdpRxQueueGetProgramBindingList(RxQueue);
    XDP_PROGRAM *Program = XdpRxQueueGetProgram(RxQueue);
    LIST_ENTRY *Entry = BindingListHead->Flink;
    UINT32 RuleIndex = 0;

    TraceEnter(TRACE_CORE, "Updating Program=%p on RxQueue=%p", Program, RxQueue);

    while (Entry != BindingListHead) {
        XDP_PROGRAM_BINDING *ProgramBinding =
            CONTAINING_RECORD(Entry, XDP_PROGRAM_BINDING, RxQueueEntry);
        const XDP_PROGRAM_OBJECT *BoundProgramObject = ProgramBinding->OwningProgram;

        TraceInfo(
            TRACE_CORE, "Compiling ProgramObject=%p into Program=%p",
            BoundProgramObject, Program);
        XdpProgramTraceObject(BoundProgramObject);

        for (UINT32 i = 0; i < BoundProgramObject->Program.RuleCount; i++) {
            Program->Rules[RuleIndex++] = BoundProgramObject->Program.Rules[i];
        }

        Entry = Entry->Flink;
    }

    //
    // If the program compiled for this newly added binding failed to be added
    // to the RX queue, we will end up having Program->RuleCount == RuleIndex.
    //
    ASSERT(Program->RuleCount >= RuleIndex);
    Program->RuleCount = RuleIndex;

    TraceInfo(TRACE_CORE, "Updated Program=%p on RxQueue=%p", Program, RxQueue);
    XdpProgramTrace(Program);
    TraceExitSuccess(TRACE_CORE);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
XdpProgramCompileNewProgram(
    _In_ XDP_RX_QUEUE *RxQueue,
    _Out_ XDP_PROGRAM **Program
    )
{
    NTSTATUS Status;
    LIST_ENTRY *BindingListHead = XdpRxQueueGetProgramBindingList(RxQueue);
    LIST_ENTRY *Entry = BindingListHead->Flink;
    UINT32 RuleCount = 0;
    XDP_PROGRAM *NewProgram;
    SIZE_T AllocationSize;

    TraceEnter(TRACE_CORE, "Compiling new program on RxQueue=%p", RxQueue);

    while (Entry != BindingListHead) {
        XDP_PROGRAM_BINDING *ProgramBinding =
            CONTAINING_RECORD(Entry, XDP_PROGRAM_BINDING, RxQueueEntry);
        Status =
            RtlUInt32Add(
                RuleCount, ProgramBinding->OwningProgram->Program.RuleCount, &RuleCount);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
        Entry = Entry->Flink;
    }

    if (RuleCount == 0) {
        //
        // No program bindings on the RX queue.
        //
        *Program = NULL;
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    Status = RtlSizeTMult(sizeof(XDP_RULE), RuleCount, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlSizeTAdd(FIELD_OFFSET(XDP_PROGRAM, Rules), AllocationSize, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    NewProgram = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM);
    if (NewProgram == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Entry = BindingListHead->Flink;
    while (Entry != BindingListHead) {
        XDP_PROGRAM_BINDING *ProgramBinding =
            CONTAINING_RECORD(Entry, XDP_PROGRAM_BINDING, RxQueueEntry);
        const XDP_PROGRAM_OBJECT *BoundProgramObject = ProgramBinding->OwningProgram;

        TraceInfo(
            TRACE_CORE, "Compiling ProgramObject=%p into Program=%p",
            BoundProgramObject, NewProgram);
        XdpProgramTraceObject(BoundProgramObject);

        for (UINT32 i = 0; i < BoundProgramObject->Program.RuleCount; i++) {
            NewProgram->Rules[NewProgram->RuleCount++] = BoundProgramObject->Program.Rules[i];
        }

        Entry = Entry->Flink;
    }

    ASSERT(NewProgram->RuleCount == RuleCount);

    TraceInfo(TRACE_CORE, "Compiled Program=%p on RxQueue=%p", NewProgram, RxQueue);
    XdpProgramTrace(NewProgram);
    *Program = NewProgram;

Exit:
    TraceExitSuccess(TRACE_CORE);
    return Status;
}

static
VOID
XdpProgramDetachRxQueue(
    _In_ XDP_PROGRAM_BINDING *ProgramBinding
    )
{
    XDP_RX_QUEUE *RxQueue = ProgramBinding->RxQueue;

    TraceEnter(
        TRACE_CORE,
        "Detach ProgramBinding=%p on ProgramObject=%p from RxQueue=%p",
        ProgramBinding, ProgramBinding->OwningProgram, ProgramBinding->RxQueue);

    ASSERT(RxQueue != NULL);
    ASSERT(!IsListEmpty(&ProgramBinding->RxQueueEntry));

    //
    // Remove the binding from the RX queue and recompile bound programs.
    //
    RemoveEntryList(&ProgramBinding->RxQueueEntry);
    InitializeListHead(&ProgramBinding->RxQueueEntry);

    XdpRxQueueDeregisterNotifications(RxQueue, &ProgramBinding->RxQueueNotificationEntry);
    if (IsListEmpty(XdpRxQueueGetProgramBindingList(ProgramBinding->RxQueue))) {
        XDP_PROGRAM *OldCompiledProgram = XdpRxQueueGetProgram(RxQueue);
        XdpRxQueueSetProgram(RxQueue, NULL, NULL, NULL);
        if (OldCompiledProgram != NULL) {
            ExFreePoolWithTag(OldCompiledProgram, XDP_POOLTAG_PROGRAM);
        }
    } else {
        //
        // Update the program in-place because we are down sizing the program bindings.
        //
        XdpRxQueueSync(RxQueue, XdpProgramUpdateCompiledProgram, RxQueue);
    }

    TraceExitSuccess(TRACE_CORE);
}

VOID
XdpProgramReleasePortSet(
    _Inout_ XDP_PORT_SET *PortSet
    )
{
    PortSet->PortSet = NULL;

    if (PortSet->Reserved != NULL) {
        MDL *Mdl = PortSet->Reserved;
        if (Mdl->MdlFlags & MDL_PAGES_LOCKED) {
            MmUnlockPages(Mdl);
        }
        IoFreeMdl(Mdl);
        PortSet->Reserved = NULL;
    }
}

NTSTATUS
XdpProgramCapturePortSet(
    _In_ const XDP_PORT_SET *UserPortSet,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Inout_ XDP_PORT_SET *KernelPortSet
    )
{
    NTSTATUS Status;

    __try {
        if (UserPortSet->Reserved != NULL) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        KernelPortSet->Reserved =
            IoAllocateMdl(
                (VOID *)UserPortSet->PortSet, XDP_PORT_SET_BUFFER_SIZE, FALSE, FALSE, NULL);
        if (KernelPortSet->Reserved == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        MmProbeAndLockPages(KernelPortSet->Reserved, RequestorMode, IoReadAccess);

        KernelPortSet->PortSet =
            MmGetSystemAddressForMdlSafe(KernelPortSet->Reserved, LowPagePriority);
        if (KernelPortSet->PortSet == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        Status = STATUS_SUCCESS;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpProgramReleasePortSet(KernelPortSet);
    }

    return Status;
}

static
VOID
XdpProgramDelete(
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    while (!IsListEmpty(&ProgramObject->ProgramBindings)) {
        XDP_PROGRAM_BINDING *ProgramBinding =
            (XDP_PROGRAM_BINDING *)ProgramObject->ProgramBindings.Flink;

        //
        // Detach the XDP program from the RX queue.
        // The binding might have already been detached during interface tear-down.
        //
        if (!IsListEmpty(&ProgramBinding->RxQueueEntry)) {
            XdpProgramDetachRxQueue(ProgramBinding);
        }

        if (ProgramBinding->RxQueue != NULL) {
            XdpRxQueueDereference(ProgramBinding->RxQueue);
        }

        RemoveEntryList(&ProgramBinding->Link);

        TraceInfo(
            TRACE_CORE, "Deleted ProgramBinding %p", ProgramBinding);
        ExFreePoolWithTag(ProgramBinding, XDP_POOLTAG_PROGRAM_BINDING);
    }

    //
    // Clean up the XDP program after data path references are dropped.
    //

    for (ULONG Index = 0; Index < ProgramObject->Program.RuleCount; Index++) {
        XDP_RULE *Rule = &ProgramObject->Program.Rules[Index];

        XdpProgramDeleteRule(Rule);
    }

    TraceVerbose(TRACE_CORE, "Deleted ProgramObject=%p", ProgramObject);
    ExFreePoolWithTag(ProgramObject, XDP_POOLTAG_PROGRAM_OBJECT);
    TraceExitSuccess(TRACE_CORE);
}

static
VOID
XdpProgramRxQueueNotify(
    XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotificationEntry,
    XDP_RX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    XDP_PROGRAM_BINDING *ProgramBinding =
        CONTAINING_RECORD(NotificationEntry, XDP_PROGRAM_BINDING, RxQueueNotificationEntry);

    switch (NotificationType) {

    case XDP_RX_QUEUE_NOTIFICATION_DELETE:
        XdpProgramDetachRxQueue(ProgramBinding);
        break;

    }
}

BOOLEAN
XdpProgramIsEbpf(
    _In_ XDP_PROGRAM *Program
    )
{
    return Program->RuleCount == 1 && Program->Rules[0].Action == XDP_PROGRAM_ACTION_EBPF;
}

BOOLEAN
XdpProgramCanXskBypass(
    _In_ XDP_PROGRAM *Program,
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    return
        Program->RuleCount == 1 &&
        Program->Rules[0].Match == XDP_MATCH_ALL &&
        Program->Rules[0].Action == XDP_PROGRAM_ACTION_REDIRECT &&
        Program->Rules[0].Redirect.TargetType == XDP_REDIRECT_TARGET_TYPE_XSK &&
        XskCanBypass(Program->Rules[0].Redirect.Target, RxQueue);
}

static
NTSTATUS
XdpProgramObjectAllocate(
    _In_ UINT32 RuleCount,
    _Out_ XDP_PROGRAM_OBJECT **NewProgramObject
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    SIZE_T AllocationSize;
    NTSTATUS Status;

    Status = RtlSizeTMult(sizeof(XDP_RULE), RuleCount, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlSizeTAdd(sizeof(*ProgramObject), AllocationSize, &AllocationSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    ProgramObject = ExAllocatePoolZero(NonPagedPoolNx, AllocationSize, XDP_POOLTAG_PROGRAM_OBJECT);
    if (ProgramObject == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    ProgramObject->CreatedByPid = (ULONG_PTR)PsGetCurrentProcessId();
    InitializeListHead(&ProgramObject->ProgramBindings);

Exit:

    *NewProgramObject = ProgramObject;
    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpCaptureProgram(
    _In_ const XDP_RULE *Rules,
    _In_ ULONG RuleCount,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Out_ XDP_PROGRAM_OBJECT **NewProgramObject
    )
{
    NTSTATUS Status;
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    XDP_PROGRAM *Program = NULL;

    TraceEnter(TRACE_CORE, "-");

    Status = XdpProgramObjectAllocate(RuleCount, &ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    TraceVerbose(TRACE_CORE, "Allocated ProgramObject=%p", ProgramObject);

    Program = &ProgramObject->Program;

    __try {
        if (RequestorMode != KernelMode) {
            ProbeForRead((VOID*)Rules, sizeof(*Rules) * RuleCount, PROBE_ALIGNMENT(XDP_RULE));
        }
        RtlCopyVolatileMemory(ProgramObject->Program.Rules, Rules, sizeof(*Rules) * RuleCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        goto Exit;
    }

    for (ULONG Index = 0; Index < RuleCount; Index++) {
        XDP_RULE UserRule = Program->Rules[Index];

        Status =
            XdpProgramValidateRule(
                &Program->Rules[Index], RequestorMode, &UserRule, RuleCount, Index);

        //
        // Whether or not the validation returns success, the program's rule
        // fields have been sanitized.
        //
        Program->RuleCount++;

        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    if (NT_SUCCESS(Status)) {
        ASSERT(ProgramObject != NULL);
        TraceInfo(TRACE_CORE, "Captured ProgramObject=%p", ProgramObject);
        XdpProgramTraceObject(ProgramObject);
        *NewProgramObject = ProgramObject;
    } else {
        if (ProgramObject != NULL) {
            XdpProgramDelete(ProgramObject);
        }
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
NTSTATUS
XdpProgramValidateIfQueue(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_opt_ VOID *ValidationContext
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = ValidationContext;
    XDP_PROGRAM *Program = &ProgramObject->Program;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    //
    // Perform further rule validation that requires an interface RX queue.
    //

    //
    // Since we don't know what an eBPF program will return, assume it will
    // return all statuses.
    //
    if (XdpProgramIsEbpf(Program) && !XdpRxQueueIsTxActionSupported(XdpRxQueueGetConfig(RxQueue))) {
        TraceError(
            TRACE_CORE, "ProgramObject=%p RX queue does not support TX action", ProgramObject);
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    for (ULONG Index = 0; Index < Program->RuleCount; Index++) {
        XDP_RULE *Rule = &Program->Rules[Index];

        if (Rule->Action == XDP_PROGRAM_ACTION_L2FWD) {
            if (!XdpRxQueueIsTxActionSupported(XdpRxQueueGetConfig(RxQueue))) {
                TraceError(
                    TRACE_CORE, "ProgramObject=%p RX queue does not support TX action",
                    ProgramObject);
                Status = STATUS_NOT_SUPPORTED;
                goto Exit;
            }
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CORE);
    return Status;
}

static
NTSTATUS
XdpProgramBindingAllocate(
    _Out_ XDP_PROGRAM_BINDING **NewProgramBinding,
    _In_ XDP_PROGRAM_OBJECT *ProgramObject
    )
{
    XDP_PROGRAM_BINDING *ProgramBinding = NULL;
    NTSTATUS Status;

    ProgramBinding =
        ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*ProgramBinding), XDP_POOLTAG_PROGRAM_BINDING);
    if (ProgramBinding == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    InitializeListHead(&ProgramBinding->RxQueueEntry);
    InitializeListHead(&ProgramBinding->Link);
    XdpRxQueueInitializeNotificationEntry(&ProgramBinding->RxQueueNotificationEntry);

    ProgramBinding->OwningProgram = ProgramObject;
    InsertTailList(&ProgramObject->ProgramBindings, &ProgramBinding->Link);

    Status = STATUS_SUCCESS;

Exit:
    *NewProgramBinding = ProgramBinding;
    return Status;
}

static
NTSTATUS
XdpProgramBindingAttach(
    _In_ XDP_BINDING_HANDLE IfHandle,
    _In_ const XDP_HOOK_ID *HookId,
    _Inout_ XDP_PROGRAM_OBJECT *ProgramObject,
    _In_ UINT32 QueueId
    )
{
    XDP_PROGRAM *Program = &ProgramObject->Program;
    XDP_PROGRAM_BINDING *ProgramBinding = NULL;
    XDP_PROGRAM *CompiledProgram = NULL;
    NTSTATUS Status;

    TraceEnter(
        TRACE_CORE, "IfHandle=%p ProgramObject=%p QueueId=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!}",
        IfHandle, ProgramObject, QueueId, HookId->Layer, HookId->Direction, HookId->SubLayer);

    Status = XdpProgramBindingAllocate(&ProgramBinding, ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpRxQueueFindOrCreate(IfHandle, HookId, QueueId, &ProgramBinding->RxQueue);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    for (ULONG Index = 0; Index < Program->RuleCount; Index++) {
        XDP_RULE *Rule = &Program->Rules[Index];

        if (Rule->Action == XDP_PROGRAM_ACTION_REDIRECT) {

            switch (Rule->Redirect.TargetType) {

            case XDP_REDIRECT_TARGET_TYPE_XSK:
                Status = XskValidateDatapathHandle(Rule->Redirect.Target);
                if (!NT_SUCCESS(Status)) {
                    goto Exit;
                }

                break;

            default:
                break;
            }
        }
    }

    XDP_PROGRAM *OldCompiledProgram = XdpRxQueueGetProgram(ProgramBinding->RxQueue);

    //
    // Do not allow eBPF programs to be replaced or to replace existing
    // programs.
    //
    if (OldCompiledProgram != NULL &&
        (XdpProgramIsEbpf(OldCompiledProgram) || XdpProgramIsEbpf(Program))) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    InsertTailList(
        XdpRxQueueGetProgramBindingList(ProgramBinding->RxQueue), &ProgramBinding->RxQueueEntry);
    Status = XdpProgramCompileNewProgram(ProgramBinding->RxQueue, &CompiledProgram);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Register for interface/queue removal notifications.
    //
    XdpRxQueueRegisterNotifications(
        ProgramBinding->RxQueue, &ProgramBinding->RxQueueNotificationEntry,
        XdpProgramRxQueueNotify);

    ASSERT(
        !IsListEmpty(&ProgramBinding->RxQueueEntry) &&
        !IsListEmpty(&ProgramBinding->Link));

    TraceInfo(
        TRACE_CORE, "Attaching ProgramBinding=%p RxQueue=%p ProgramObject=%p",
        ProgramBinding, ProgramBinding->RxQueue, ProgramObject);
    XdpProgramTraceObject(ProgramObject);

    Status =
        XdpRxQueueSetProgram(
            ProgramBinding->RxQueue, CompiledProgram, XdpProgramValidateIfQueue,
            ProgramObject);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            TRACE_CORE, "Failed to attach ProgramObject=%p to RxQueue=%p Status=%!STATUS!",
            ProgramObject, ProgramBinding->RxQueue, Status);
        goto Exit;
    }

    CompiledProgram = NULL;

    if (OldCompiledProgram != NULL) {
        //
        // We just swapped in a new compiled program. Delete the old one.
        //
        ExFreePoolWithTag(OldCompiledProgram, XDP_POOLTAG_PROGRAM);
        OldCompiledProgram = NULL;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        if (CompiledProgram != NULL) {
            ExFreePoolWithTag(CompiledProgram, XDP_POOLTAG_PROGRAM);
        }
    }

    TraceExitStatus(TRACE_CORE);
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
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    UINT32 QueueIdStart = Item->QueueId;
    UINT32 QueueIdEnd = Item->QueueId + 1;
    XDP_IFSET_HANDLE IfSetHandle = NULL;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    if (Item->HookId.SubLayer != XDP_HOOK_INSPECT) {
        //
        // Only RX queue programs are currently supported.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (Item->BindToAllQueues) {
        XDP_IF_OFFLOAD_HANDLE InterfaceOffloadHandle;
        XDP_RSS_CAPABILITIES RssCapabilities;
        UINT32 RssCapabilitiesSize = sizeof(RssCapabilities);
        IfSetHandle = XdpIfFindAndReferenceIfSet(Item->IfIndex, &Item->HookId, 1, NULL);
        if (IfSetHandle == NULL) {
            Status = STATUS_NOT_FOUND;
            goto Exit;
        }

        Status =
            XdpIfOpenInterfaceOffloadHandle(
                IfSetHandle, &Item->HookId, &InterfaceOffloadHandle);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status =
            XdpIfGetInterfaceOffloadCapabilities(
                IfSetHandle, InterfaceOffloadHandle,
                XdpOffloadRss, &RssCapabilities, &RssCapabilitiesSize);
        XdpIfCloseInterfaceOffloadHandle(IfSetHandle, InterfaceOffloadHandle);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        TraceInfo(
            TRACE_CORE, "Attaching ProgramObject=%p to all %u queues",
            ProgramObject, RssCapabilities.NumberOfReceiveQueues);
        QueueIdStart = 0;

        //
        // Ensure the implicit 0th queue is bound when RSS is not supported.
        //
        QueueIdEnd = max(1, RssCapabilities.NumberOfReceiveQueues);
    }

    for (UINT32 QueueId = QueueIdStart; QueueId < QueueIdEnd; ++QueueId) {
        Status =
            XdpProgramBindingAttach(
                Item->Bind.BindingHandle, &Item->HookId, ProgramObject, QueueId);
        if (!NT_SUCCESS(Status)) {
            //
            // Failed to attach to one of the RX queues.
            //
            // TODO: should we allow it to succeed partially?
            //
            goto Exit;
        }
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        XdpProgramDelete(ProgramObject);
    }

    if (IfSetHandle != NULL) {
        XdpIfDereferenceIfSet(IfSetHandle);
    }

    Item->CompletionStatus = Status;
    KeSetEvent(Item->CompletionEvent, 0, FALSE);

    TraceExitStatus(TRACE_CORE);
}

static
VOID
XdpProgramDetach(
    _In_ XDP_BINDING_WORKITEM *WorkItem
    )
{
    XDP_PROGRAM_WORKITEM *Item = (XDP_PROGRAM_WORKITEM *)WorkItem;
    XDP_PROGRAM_OBJECT *ProgramObject = Item->ProgramObject;
    XDP_PROGRAM *Program = &Item->ProgramObject->Program;
    EBPF_EXTENSION_CLIENT *Client = NULL;
    KEVENT *CompletionEvent = Item->CompletionEvent;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    if (XdpProgramIsEbpf(Program)) {
        Client = XdpGetEbpfExtensionClientFromProgram(Program);
    }

    XdpIfDereferenceBinding(ProgramObject->IfHandle);
    XdpProgramDelete(ProgramObject);
    TraceInfo(TRACE_CORE, "Detached ProgramObject=%p", ProgramObject);

    if (Client != NULL) {
        EbpfExtensionDetachClientCompletion(Client);
    }

    if (CompletionEvent != NULL) {
        KeSetEvent(CompletionEvent, 0, FALSE);
    }

    TraceExitSuccess(TRACE_CORE);
}

static
NTSTATUS
XdpProgramCreate(
    _Out_ XDP_PROGRAM_OBJECT **NewProgramObject,
    _In_ const XDP_PROGRAM_OPEN *Params,
    _In_ KPROCESSOR_MODE RequestorMode
    )
{
    XDP_INTERFACE_MODE InterfaceMode;
    XDP_INTERFACE_MODE *RequiredMode = NULL;
    XDP_BINDING_HANDLE BindingHandle = NULL;
    XDP_PROGRAM_WORKITEM WorkItem = {0};
    KEVENT CompletionEvent;
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    NTSTATUS Status;
    const UINT32 ValidFlags =
        XDP_CREATE_PROGRAM_FLAG_GENERIC |
        XDP_CREATE_PROGRAM_FLAG_NATIVE |
        XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES;

    TraceEnter(
        TRACE_CORE,
        "IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Flags=%x",
        Params->IfIndex, Params->HookId.Layer, Params->HookId.Direction, Params->HookId.SubLayer,
        Params->QueueId, Params->Flags);

    if ((Params->Flags & ~ValidFlags) ||
        !RTL_IS_CLEAR_OR_SINGLE_FLAG(
            Params->Flags, XDP_CREATE_PROGRAM_FLAG_GENERIC | XDP_CREATE_PROGRAM_FLAG_NATIVE)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (Params->Flags & XDP_CREATE_PROGRAM_FLAG_GENERIC) {
        InterfaceMode = XDP_INTERFACE_MODE_GENERIC;
        RequiredMode = &InterfaceMode;
    }

    if (Params->Flags & XDP_CREATE_PROGRAM_FLAG_NATIVE) {
        InterfaceMode = XDP_INTERFACE_MODE_NATIVE;
        RequiredMode = &InterfaceMode;
    }

RetryBinding:

    BindingHandle = XdpIfFindAndReferenceBinding(Params->IfIndex, &Params->HookId, 1, RequiredMode);
    if (BindingHandle == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Status =
        XdpCaptureProgram(Params->Rules, Params->RuleCount, RequestorMode, &ProgramObject);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    KeInitializeEvent(&CompletionEvent, NotificationEvent, FALSE);
    WorkItem.CompletionEvent = &CompletionEvent;
    WorkItem.QueueId = Params->QueueId;
    WorkItem.HookId = Params->HookId;
    WorkItem.IfIndex = Params->IfIndex;
    WorkItem.BindToAllQueues = !!(Params->Flags & XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES);
    WorkItem.ProgramObject = ProgramObject;
    WorkItem.Bind.BindingHandle = BindingHandle;
    WorkItem.Bind.WorkRoutine = XdpProgramAttach;

    //
    // Attach the program using the interface's work queue.
    //
    XdpIfQueueWorkItem(&WorkItem.Bind);
    KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);

    Status = WorkItem.CompletionStatus;

    if (!NT_SUCCESS(Status) &&
        XdpIfGetCapabilities(BindingHandle)->Mode == XDP_INTERFACE_MODE_NATIVE &&
        RequiredMode == NULL) {
        //
        // The program failed to attach to the native interface. Since the
        // application did not require native mode, attempt to fall back to
        // generic mode.
        //
        TraceInfo(
            TRACE_CORE,
            "IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Status=%!STATUS! native mode failed, trying generic mode",
            Params->IfIndex, Params->HookId.Layer, Params->HookId.Direction,
            Params->HookId.SubLayer, Params->QueueId, Status);

        ProgramObject = NULL;
        XdpIfDereferenceBinding(BindingHandle);
        BindingHandle = NULL;

        InterfaceMode = XDP_INTERFACE_MODE_GENERIC;
        RequiredMode = &InterfaceMode;
        goto RetryBinding;
    }

Exit:

    if (NT_SUCCESS(Status)) {
        ProgramObject->IfHandle = BindingHandle, BindingHandle = NULL;
        *NewProgramObject = ProgramObject;
    }

    if (BindingHandle != NULL) {
        XdpIfDereferenceBinding(BindingHandle);
    }

    TraceInfo(
        TRACE_CORE,
        "ProgramObject=%p IfIndex=%u Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} QueueId=%u Flags=%x Status=%!STATUS!",
        ProgramObject, Params->IfIndex, Params->HookId.Layer, Params->HookId.Direction,
        Params->HookId.SubLayer, Params->QueueId, Params->Flags, Status);

    TraceExitStatus(TRACE_CORE);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpCreateProgram(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ const XDP_OPEN_PACKET *OpenPacket,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    const XDP_PROGRAM_OPEN *Params = NULL;
    XDP_PROGRAM_OBJECT *ProgramObject = NULL;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "Irp=%p", Irp);

    UNREFERENCED_PARAMETER(OpenPacket);

    if (Disposition != FILE_CREATE || InputBufferLength < sizeof(*Params)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    Params = InputBuffer;

    Status = XdpProgramCreate(&ProgramObject, Params, Irp->RequestorMode);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (NT_SUCCESS(Status)) {
        ProgramObject->Header.ObjectType = XDP_OBJECT_TYPE_PROGRAM;
        ProgramObject->Header.Dispatch = &XdpProgramFileDispatch;
        IrpSp->FileObject->FsContext = ProgramObject;
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
VOID
XdpProgramClose(
    _In_ XDP_PROGRAM_OBJECT *ProgramObject,
    _In_ BOOLEAN Wait
    )
{
    KEVENT CompletionEvent;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);
    TraceInfo(TRACE_CORE, "Closing ProgramObject=%p", ProgramObject);

    if (Wait) {
        KeInitializeEvent(&CompletionEvent, NotificationEvent, FALSE);
        ProgramObject->CloseWorkItem.CompletionEvent = &CompletionEvent;
    }

    ProgramObject->CloseWorkItem.ProgramObject = ProgramObject;
    ProgramObject->CloseWorkItem.Bind.BindingHandle = ProgramObject->IfHandle;
    ProgramObject->CloseWorkItem.Bind.WorkRoutine = XdpProgramDetach;

    //
    // Perform the detach on the interface's work queue.
    //
    XdpIfQueueWorkItem(&ProgramObject->CloseWorkItem.Bind);

    if (Wait) {
        KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
    }

    TraceExitSuccess(TRACE_CORE);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTSTATUS
XdpIrpProgramClose(
    _Inout_ IRP *Irp,
    _Inout_ IO_STACK_LOCATION *IrpSp
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = IrpSp->FileObject->FsContext;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    UNREFERENCED_PARAMETER(Irp);

    XdpProgramClose(ProgramObject, TRUE);

    TraceExitSuccess(TRACE_CORE);

    return STATUS_SUCCESS;
}

static
NTSTATUS
EbpfProgramOnClientAttach(
    _In_ const EBPF_EXTENSION_CLIENT *AttachingClient,
    _In_ const EBPF_EXTENSION_PROVIDER *AttachingProvider
    )
{
    NTSTATUS Status;
    const ebpf_extension_data_t *ClientData = EbpfExtensionClientGetClientData(AttachingClient);
    const ebpf_extension_dispatch_table_t *ClientDispatch =
        EbpfExtensionClientGetDispatch(AttachingClient);
    UINT32 IfIndex;
    XDP_PROGRAM_OPEN OpenParams = {0};
    XDP_RULE XdpRule = {0};
    XDP_PROGRAM_OBJECT *ProgramObject;
    ULONG RequiredMode;

    TraceEnter(
        TRACE_CORE, "AttachingProvider=%p AttachingClient=%p", AttachingProvider, AttachingClient);

    if (ClientData == NULL ||
        ClientData->header.version < EBPF_ATTACH_CLIENT_DATA_CURRENT_VERSION ||
        ClientData->data_size != sizeof(IfIndex) ||
        ClientData->data == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    IfIndex = *(UINT32 *)ClientData->data;

    if (IfIndex == IFI_UNSPECIFIED) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (ClientDispatch == NULL || ClientDispatch->version != 1 || ClientDispatch->count < 4) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    OpenParams.IfIndex = IfIndex;
    OpenParams.HookId.Layer = XDP_HOOK_L2;
    OpenParams.HookId.Direction = XDP_HOOK_RX;
    OpenParams.HookId.SubLayer = XDP_HOOK_INSPECT;
    OpenParams.Flags = XDP_CREATE_PROGRAM_FLAG_ALL_QUEUES;
    OpenParams.RuleCount = 1;
    OpenParams.Rules = &XdpRule;

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XdpEbpfMode", &RequiredMode);
    if (NT_SUCCESS(Status)) {
        switch (RequiredMode) {
        case XDP_INTERFACE_MODE_GENERIC:
            OpenParams.Flags |= XDP_CREATE_PROGRAM_FLAG_GENERIC;
            break;

        case XDP_INTERFACE_MODE_NATIVE:
            OpenParams.Flags |= XDP_CREATE_PROGRAM_FLAG_NATIVE;
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
    }

    XdpRule.Match = XDP_MATCH_ALL;
    XdpRule.Action = XDP_PROGRAM_ACTION_EBPF;
    XdpRule.Ebpf.Target = (HANDLE)AttachingClient;

    Status = XdpProgramCreate(&ProgramObject, &OpenParams, KernelMode);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    EbpfExtensionClientSetProviderData(AttachingClient, ProgramObject);

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
NTSTATUS
EbpfProgramOnClientDetach(
    _In_ const EBPF_EXTENSION_CLIENT *DetachingClient
    )
{
    XDP_PROGRAM_OBJECT *ProgramObject = EbpfExtensionClientGetProviderData(DetachingClient);
    NTSTATUS Status = STATUS_PENDING;

    TraceEnter(TRACE_CORE, "ProgramObject=%p", ProgramObject);

    ASSERT(ProgramObject != NULL);

    XdpProgramClose(ProgramObject, FALSE);

    TraceExitStatus(TRACE_CORE);

    return Status;
}

NTSTATUS
XdpProgramStart(
    VOID
    )
{
    const EBPF_EXTENSION_PROVIDER_PARAMETERS EbpfProgramInfoProviderParameters = {
        .ProviderModuleId = &EbpfXdpProgramInfoProviderModuleId,
        .ProviderData = &EbpfXdpProgramData,
    };
    const EBPF_EXTENSION_PROVIDER_PARAMETERS EbpfHookProviderParameters = {
        .ProviderModuleId = &EbpfXdpHookProviderModuleId,
        .ProviderData = &EbpfXdpHookAttachProviderData,
    };
    DWORD EbpfEnabled;
    NTSTATUS Status;

    TraceEnter(TRACE_CORE, "-");

    //
    // eBPF is disabled by default while reliability bugs are outstanding.
    //

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XdpEbpfEnabled", &EbpfEnabled);
    if (NT_SUCCESS(Status) && EbpfEnabled) {
        Status =
            EbpfExtensionProviderRegister(
                &EBPF_PROGRAM_INFO_EXTENSION_IID, &EbpfProgramInfoProviderParameters, NULL, NULL,
                NULL, &EbpfXdpProgramInfoProvider);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status =
            EbpfExtensionProviderRegister(
                &EBPF_HOOK_EXTENSION_IID, &EbpfHookProviderParameters, EbpfProgramOnClientAttach,
                EbpfProgramOnClientDetach, NULL, &EbpfXdpProgramHookProvider);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CORE);

    return Status;
}

VOID
XdpProgramStop(
    VOID
    )
{
    TraceEnter(TRACE_CORE, "-");

    if (EbpfXdpProgramHookProvider != NULL) {
        EbpfExtensionProviderUnregister(EbpfXdpProgramHookProvider);
        EbpfXdpProgramHookProvider = NULL;
    }

    if (EbpfXdpProgramInfoProvider != NULL) {
        EbpfExtensionProviderUnregister(EbpfXdpProgramInfoProvider);
        EbpfXdpProgramInfoProvider = NULL;
    }

    TraceExitSuccess(TRACE_CORE);
}
