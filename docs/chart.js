
// Fixed charting values
var dataMaxCount = 90
var dataLineWidth = 2
var dataRawPointRadius = 1

function generateDataset(allData, name) {
    var data = allData.filter(x => x.name === name)
    var output = []
    data.forEach(
        p => output.push({commit:p.commit, x:new Date(p.time * 1000), y:p.p0}))
    return output
}

function createDataset(name, data) {
    return {
        type: "line",
        label: name,
        borderWidth: dataLineWidth,
        pointRadius: dataRawPointRadius,
        tension: 0,
        data: data.slice(-dataMaxCount)
    };
}

function titlePlacement(tooltipItem, data) {
    var dataset = data.datasets[tooltipItem[0].datasetIndex]
    var datapoint = dataset.data[tooltipItem[0].index]
    return new Date(datapoint.x).toString()
}

function createChartwithData(allData, year, chart, names, displayLegend, stacked) {
    var dataset = [];
    names.forEach(
        name => dataset.push(createDataset(name, generateDataset(allData, chart + "-" + name))))
    new Chart(document.getElementById("canvas-"+year+"-"+chart).getContext('2d'), {
        data: { datasets: dataset },
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

window.onload = function() {
    processSearchParams()
    // Read in the latest data.
    var years = ['2019', '2022']
    years.forEach(year =>
        fetch('https://xdpperfdata.blob.core.windows.net/cidata/xskperf-ws'+year+'.csv')
        .then(response => response.text())
        .then(text => createChart(processData(text), year)))
};
