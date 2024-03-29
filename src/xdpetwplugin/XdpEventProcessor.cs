﻿//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

using System;
using System.Collections.Generic;
using Microsoft.Performance.SDK.Extensibility;
using Microsoft.Performance.SDK.Extensibility.SourceParsing;
using Microsoft.Performance.SDK.Processing;
using XdpEtw.DataModel;

namespace XdpEtw
{
    public class XdpEventProcessor : CustomDataProcessorWithSourceParser<XdpEvent, object, Guid>
    {
        internal XdpEventProcessor(
            ISourceParser<XdpEvent, object, Guid> sourceParser,
            ProcessorOptions options,
            IApplicationEnvironment applicationEnvironment,
            IProcessorEnvironment processorEnvironment)
            : base(sourceParser, options, applicationEnvironment, processorEnvironment)
        {
        }
    }
}
