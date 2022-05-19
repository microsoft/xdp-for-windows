//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

using System;
using Microsoft.Performance.SDK;
using Microsoft.Performance.SDK.Extensibility;

#pragma warning disable CA1305 // Specify IFormatProvider

namespace XdpEtw.DataModel
{
    public enum XdpEventParseMode
    {
        Full,
        WpaFilter
    }

    public enum XdpObjectType
    {
        Xsk,
        GenericTxQueue,
        GenericRxFilter,
    }

    public enum XdpEventId : ushort
    {
        XskNotifyPokeStart      = 1,
        XskNotifyPokeStop       = 2,
        XskCreateSocket         = 3,
        XskCloseSocketStart     = 4,
        XskCloseSocketStop      = 5,
        XskTxEnqueue            = 6,
        XskTxBind               = 7,
        GenericTxEnqueue        = 8,
        GenericTxPostBatchStart = 9,
        GenericTxPostBatchStop  = 10,
        GenericRxInspectStart   = 11,
        GenericRxInspectStop    = 12,
        XskRxPostBatch          = 13,
    }

    //
    // The base class for all XDP events.
    //
    public class XdpEvent : IKeyedDataType<Guid>, IComparable<Guid>
    {
        //
        // Global configuration to control how parsing works. Defaults to WPA filter mode.
        //
        public static XdpEventParseMode ParseMode { get; set; } = XdpEventParseMode.WpaFilter;

        //
        // The provider GUID used for XDP ETW on Windows.
        //
        public static readonly Guid ProviderGuid = new Guid("580bbdea-b364-4369-b291-d3539e35d20b");

        public XdpEventId EventId { get; }

        public XdpObjectType ObjectType { get; }

        public Timestamp TimeStamp { get; }

        public ushort Processor { get; }

        public uint ProcessId { get; }

        public uint ThreadId { get; }

        public int PointerSize { get; }

        public ulong ObjectPointer { get; }

        public virtual string PrefixString => PrefixStrings[(int)ObjectType];

        public virtual string HeaderString =>
            string.Format("[{0,2}][{1,5:X}][{2,5:X}][{3}][{4}][{5:X}]",
                Processor, ProcessId, ThreadId, TimeStamp.ToTimeSpan, PrefixString, ObjectPointer);

        public virtual string PayloadString => string.Format("[{0}]", EventId);

        public override string ToString()
        {
            return string.Format("{0} {1}", HeaderString, PayloadString);
        }

        #region Internal

        static readonly string[] PrefixStrings = new string[]
        {
            " xsk",
            "gxtq",
            "gxrf",
        };

        internal XdpEvent(XdpEventId id, XdpObjectType objectType, Timestamp timestamp, ushort processor, uint processId, uint threadId, int pointerSize, ulong objectPointer = 0)
        {
            EventId = id;
            ObjectType = objectType;
            TimeStamp = timestamp;
            Processor = processor;
            ProcessId = processId;
            ThreadId = threadId;
            PointerSize = pointerSize;
            ObjectPointer = objectPointer;
        }

        public int CompareTo(Guid other)
        {
            return ProviderGuid.CompareTo(other);
        }

        public Guid GetKey() => ProviderGuid;

        #endregion
    }
}
