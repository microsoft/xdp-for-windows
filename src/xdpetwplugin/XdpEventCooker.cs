//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Microsoft.Performance.SDK;
using Microsoft.Performance.SDK.Extensibility;
using Microsoft.Performance.SDK.Extensibility.DataCooking;
using Microsoft.Performance.SDK.Extensibility.DataCooking.SourceDataCooking;
using XdpEtw.DataModel;

namespace XdpEtw
{
    public sealed class XdpEventCooker : CookedDataReflector, ISourceDataCooker<XdpEvent, object, Guid>
    {
        public static readonly DataCookerPath CookerPath = DataCookerPath.ForSource(XdpEventParser.SourceId, "XDP");

        public ReadOnlyHashSet<Guid> DataKeys => new ReadOnlyHashSet<Guid>(new HashSet<Guid>());

        public DataCookerPath Path => CookerPath;

        public string Description => "Xdp Event Cooker";

        public IReadOnlyDictionary<DataCookerPath, DataCookerDependencyType> DependencyTypes =>
            new Dictionary<DataCookerPath, DataCookerDependencyType>();

        public IReadOnlyCollection<DataCookerPath> RequiredDataCookers => new HashSet<DataCookerPath>();

        public DataProductionStrategy DataProductionStrategy { get; }

        public SourceDataCookerOptions Options => SourceDataCookerOptions.ReceiveAllDataElements;

        public XdpEventCooker() : base(CookerPath)
        {
        }

        public void BeginDataCooking(ICookedDataRetrieval dependencyRetrieval, CancellationToken cancellationToken)
        {
        }

        public DataProcessingResult CookDataElement(XdpEvent data, object context, CancellationToken cancellationToken)
        {
            Debug.Assert(!(data is null));
            return DataProcessingResult.Processed;
        }

        public void EndDataCooking(CancellationToken cancellationToken)
        {
        }
    }
}
