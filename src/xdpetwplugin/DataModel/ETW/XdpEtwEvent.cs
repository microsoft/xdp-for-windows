//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

using System;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Performance.SDK;

namespace XdpEtw.DataModel.ETW
{
    [Flags]
    internal enum XdpEtwEventKeywords : ulong
    {
        Tx              = 0x0000000000000001ul,
        Rx              = 0x0000000000000002ul,
        Xsk             = 0x0000000000000004ul,
        Generic         = 0x0000000000000008ul,
    }

    internal enum XdpEtwEventOpcode : byte
    {
        Xsk             = 11,
        GenericTxQueue  = 12,
        GenericRxFilter = 13,
    }

    internal static class XdpEtwEvent
    {
        private static XdpObjectType ComputeObjectType(TraceEvent evt)
        {
            return (XdpObjectType)((byte)evt.Opcode - (byte)XdpEtwEventOpcode.Xsk);
        }

        internal static unsafe XdpEvent? TryCreate(TraceEvent evt, Timestamp timestamp)
        {
            var id = (XdpEventId)evt.ID;
            var processor = (ushort)evt.ProcessorNumber;
            var processId = (uint)evt.ProcessID;
            var threadId = (uint)evt.ThreadID;
            var pointerSize = evt.PointerSize;
            var data = new XdpEtwDataReader(evt.DataStart.ToPointer(), evt.EventDataLength, pointerSize);

            switch (id)
            {
                default:
                    return null;
            }
        }
    }
}
