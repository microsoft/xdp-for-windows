//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

using System;
using System.Collections.Generic;
using System.Threading;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Performance.SDK;
using Microsoft.Performance.SDK.Extensibility.SourceParsing;
using Microsoft.Performance.SDK.Processing;
using XdpEtw.DataModel;
using XdpEtw.DataModel.ETW;

#pragma warning disable CA1031 // Do not catch general exception types

namespace XdpEtw
{
    public sealed class XdpEventParser : SourceParser<XdpEvent, object, Guid>
    {
        public const string SourceId = "XDP";

        public override string Id => SourceId;

        private DataSourceInfo? info;

        private readonly IEnumerable<string> filePaths;

        public override DataSourceInfo DataSourceInfo => info ?? throw new InvalidOperationException("Data Source has not been processed");

        public XdpEventParser(IEnumerable<string> filePaths)
        {
            this.filePaths = filePaths;
        }

        public override void ProcessSource(ISourceDataProcessor<XdpEvent, object, Guid> dataProcessor, ILogger logger, IProgress<int> progress, CancellationToken cancellationToken)
        {
        }

        #region ETW

        private static readonly Guid EventTraceGuid = new Guid("68fdd900-4a3e-11d1-84f4-0000f80464e3");
        private static readonly Guid SystemConfigExGuid = new Guid("9B79EE91-B5FD-41c0-A243-4248E266E9D0");

        private static bool IsKnownSynthEvent(TraceEvent evt)
        {
            return evt.ProviderGuid == EventTraceGuid || evt.ProviderGuid == SystemConfigExGuid;
        }

        private void ProcessEtwSource(ISourceDataProcessor<XdpEvent, object, Guid> dataProcessor, IProgress<int> progress, CancellationToken cancellationToken)
        {
            using var source = new ETWTraceEventSource(filePaths);
            long StartTime = 0;

            source.AllEvents += (evt) =>
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    source.StopProcessing();
                    return;
                }

                if (info == null && evt.TimeStamp.Ticks != 0 && !IsKnownSynthEvent(evt))
                {
                    StartTime = evt.TimeStamp.Ticks;

                    var firstEventNano = (evt.TimeStamp.Ticks - source.SessionStartTime.Ticks) * 100;
                    var lastEventNano = (source.SessionEndTime.Ticks - source.SessionStartTime.Ticks) * 100;
                    info = new DataSourceInfo(firstEventNano, lastEventNano, evt.TimeStamp.ToUniversalTime());
                }
                else if (evt.ProviderGuid == XdpEvent.ProviderGuid)
                {
                    try
                    {
                        var xdpEvent = XdpEtwEvent.TryCreate(evt, new Timestamp((evt.TimeStamp.Ticks - StartTime) * 100));
                        if (xdpEvent != null)
                        {
                            dataProcessor.ProcessDataElement(xdpEvent, this, cancellationToken);
                        }
                    }
                    catch
                    {
                    }
                }

                progress.Report((int)(evt.TimeStampRelativeMSec / source.SessionEndTimeRelativeMSec * 100));
            };

            source.Process();

            if (info == null)
            {
                info = new DataSourceInfo(0, (source.SessionEndTime.Ticks - source.SessionStartTime.Ticks) * 100, source.SessionStartTime.ToUniversalTime());
            }
        }

        #endregion
    }
}
