
// Fixed charting values
var dataMaxCount = 90
var dataLineWidth = 2
var dataRawPointRadius = 1

// Allow URL parameters to overwrite the values above
function processSearchParams() {
    var url = new URL(window.location.href);
    var param = url.searchParams.get("count")
    if (param) {
        dataMaxCount = param
    }
    var param = url.searchParams.get("width")
    if (param) {
        dataLineWidth = param
    }
    var param = url.searchParams.get("radius")
    if (param) {
        dataRawPointRadius = param
    }
}

// Split the CSV data into more Javascript friendly types
// csv format: name,commit,unix_time,p0,p1,etc.
function processData(text) {
    var allData = []
    var lines = text.split(/\r\n|\n/);
    lines.forEach(
        line => {
            var data = []
            var raw = line.split(',')
            data['name'] = raw[0]
            data['commit'] = raw[1]
            data['time'] = raw[2]
            for (var i=3; i<raw.length; i++) {
                data['p' + (i-3)] = raw[i]
            }
            allData.push(data);
        })
    return allData
}

// Find all the data matching the names and create a dataset for it
function createDataset(chartName, dataName, allData) {
    var data = allData.filter(x => x.name === chartName + "-" + dataName)
    var dataset = []
    data.forEach(
        p => dataset.push({commit:p.commit, x:new Date(p.time * 1000), y:p.p0}))
    return {
        type: "line",
        label: dataName,
        borderWidth: dataLineWidth,
        pointRadius: dataRawPointRadius,
        tension: 0,
        data: dataset.slice(-dataMaxCount)
    };
}

function titlePlacement(tooltipItem, data) {
    var dataset = data.datasets[tooltipItem[0].datasetIndex]
    var datapoint = dataset.data[tooltipItem[0].index]
    return new Date(datapoint.x).toString()
}

// Create everything necessary for the new chart
function createChartwithData(allData, year, chart, names, displayLegend, stacked) {
    // Generate the chart datasets
    var datasets = [];
    names.forEach(name => datasets.push(createDataset(chart, name, allData)))

    // Create the HTML elements first
    const chartParent = document.getElementById('chart-parent');
    const div = document.createElement('div');
    div.classList.add('col-xl-12', 'col-lg-12');
    const card = document.createElement('div');
    card.classList.add('card', 'shadow', 'mb-4');
    const cardHeader = document.createElement('div');
    cardHeader.classList.add('card-header', 'py-3', 'd-flex', 'flex-row', 'align-items-center', 'justify-content-between');
    const a = document.createElement('a');
    a.classList.add('m-0', 'font-weight-bold', 'text-dark');
    a.textContent = chart+" ("+year+")";
    const cardBody = document.createElement('div');
    cardBody.classList.add('card-body');
    const chartArea = document.createElement('div');
    chartArea.classList.add('chart-area');
    const canvas = document.createElement('canvas');
    chartArea.appendChild(canvas);
    cardBody.appendChild(chartArea);
    cardHeader.appendChild(a);
    card.appendChild(cardHeader);
    card.appendChild(cardBody);
    div.appendChild(card);
    chartParent.appendChild(div);

    // Add the chart to the HTML element
    new Chart(canvas.getContext('2d'), {
        data: { datasets: datasets },
        options: {
            maintainAspectRatio: false,
            scales: {
                xAxes: [{
                    type: 'linear',
                    gridLines: {
                      display: false,
                      drawBorder: false
                    },
                    ticks: {
                        callback: function(value) {
                            return new Date(value).toDateString()
                        }
                    }
                }],
                yAxes: [{
                    stacked: stacked,
                    gridLines: {
                        color: "rgb(234, 236, 244)",
                        zeroLineColor: "rgb(234, 236, 244)",
                        drawBorder: false,
                        borderDash: [2],
                        zeroLineBorderDash: [2]
                    }
                }]
            },
            legend: {
              display: displayLegend
            },
            tooltips: {
                backgroundColor: "rgb(255,255,255)",
                bodyFontColor: "#858796",
                titleMarginBottom: 10,
                titleFontColor: '#6e707e',
                titleFontSize: 14,
                borderColor: '#dddfeb',
                borderWidth: 1,
                mode: "nearest",
                intersect: false,
                callbacks : {
                    title: titlePlacement
                }
            }
        }
    })
}

// Create all the charts
function createChart(allData, year) {
    var names = [
        'RX-BUSY-2048chunksize-64iosize-FNDIS',
        'RX-BUSY-2048chunksize-1514iosize-FNDIS',
        'TX-BUSY-2048chunksize-64iosize-FNDIS',
        'TX-BUSY-2048chunksize-1514iosize-FNDIS',
        'FWD-BUSY-2048chunksize-64iosize-FNDIS',
        'FWD-BUSY-2048chunksize-1514iosize-FNDIS'
    ]
    createChartwithData(allData, year, "XDPMP-NATIVE", names, true, false)
    createChartwithData(allData, year, "XDPMP-GENERIC", names, true, false)
}

// Immediately triggers on load of the HTML file. Loads all data and generates charts
window.onload = function() {
    processSearchParams()
    // Read in the latest data.
    var years = ['2019', '2022']
    years.forEach(year =>
        fetch('https://xdpperfdata.blob.core.windows.net/cidata/xskperf-ws'+year+'.csv')
        .then(response => response.text())
        .then(text => createChart(processData(text), year)))
};
