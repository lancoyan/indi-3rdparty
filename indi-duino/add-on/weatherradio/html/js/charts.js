function createWeatherChart(category, align, max, precision) {

    var chart = {
	height: 250,
	type: "area",
	toolbar: {show: false},
    };
    var title = {
	text: category,
	align: align,
	offsetX: 6,
	offsetY: 15,
	style: {fontSize: '14px', color: '#ccc'}
    };
    var xaxis = {
	type: "datetime",
	labels: {style: {colors: '#ccc'}}
    };
    var yaxis = {
	labels: {style: {color: '#ccc'}},
	decimalsInFloat: precision,
	max: max
    };

    return {chart: chart,
	    subtitle: title,
	    xaxis: xaxis,
	    yaxis: yaxis,
	    series: [],
	    stroke: {curve: 'smooth', width: 2},
	    legend: {labels: {colors: ['#ccc']}},
	    tooltip: {x: {format: "dd MMM yy, HH:mm"}},
	    dataLabels: {enabled: false}};
}

function createSparklineWeatherChart(category) {

    return {
	chart: {
	    height: 120,
	    type: "area",
	    sparkline: {enabled: true},
	},
	title: {
	    text: category,
	    align: 'center',
	    offsetX: -10,
	    offsetY: 10,
	    style: {fontSize: '12px', color: '#ccc'}
	},
	series: [],
	xaxis: {type: "datetime"},
	stroke: {curve: 'smooth', width: 2},
	tooltip: {x: {format: "dd MMM yy, HH:mm"}}};
}

function createRadialBarChart(name, unit, min, max, precision) {
    return ({
	chart: {
	    height: 300,
	    type: 'radialBar',
	},
	// set to 0
	series: [100 * (0 - min) / (max - min)],
	labels: [name],
	stroke: {
	    dashArray: 3
	},
	plotOptions: {
	    radialBar: {
		startAngle: -135,
		endAngle: 135,
		dataLabels: {
		    name: {show: true, offsetY: -15},
		    value: {
			show: true,
			offsetY: 0,
			fontSize: "30px",
			color: "#ccc",
			formatter: function(val) {
			    return (min + (max - min) * val/100).toFixed(precision) + unit;
			},
		    }
		},
		track: {show: true, opacity: 0.1},
	    }
	},
    });

}

function createBarChart(name, unit, min, max) {
    return ({
        series: [
	    {
		name: name,
		data: [50]
	    }],
        chart: {
	    type: 'bar',
	    height: 8,
	    width: "67%",
	    toolbar: {show: false},
	    sparkline: {enabled: true}
	},
        plotOptions: {
	    bar: {
		horizontal: true,
		colors: {
		    backgroundBarColors: ["#eee"],
		    backgroundBarOpacity: 0.1
		}
	    }
	},
        dataLabels: {enabled: false},
        xaxis: {
	    show: false,
	    categories: [name],
	    labels: {show: false},
	    axisTicks: {show: false},
	},
	yaxis: {labels: {show: false}, min: min, max: max},
	tooltip: {enabled: false}
    });
}


var hchart, cchart, tchart, pchart, schart;
var temperature, humidity, pressure, cloudCoverage, sqm;

var settings = {t_min: -40, t_max: 50, t_prec: 1,
		p_min: 973, p_max: 1053, p_prec: 0,
		sqm_min: 0, sqm_max: 25, sqm_prec: 1};

function init() {

    // create charts for current values
    temperature = new ApexCharts(document.querySelector("#temperature"),
				 createRadialBarChart('Temperature', '°C',
						      settings.t_min, settings.t_max, settings.t_prec));
    humidity = new ApexCharts(document.querySelector("#humidity"),
			      createBarChart('Humidity', '%', 0, 100));
    pressure = new ApexCharts(document.querySelector("#pressure"),
			      createRadialBarChart('Pressure', ' hPa',
						   settings.p_min, settings.p_max, settings.p_prec));
    cloudCoverage = new ApexCharts(document.querySelector("#clouds"),
				   createRadialBarChart('Cloud Coverage', '%', 0, 100, 0));
    temperature.render();
    humidity.render();
    pressure.render();
    cloudCoverage.render();

    // special case for temperature
    temperature.updateOptions({
    }, true, false, false);
    
    // create the time series charts
    
    tchart = new ApexCharts(document.querySelector("#temperature_series"),
			    createWeatherChart("Temperature", "left", undefined, 1));
    hchart = new ApexCharts(document.querySelector("#humidity_series"),
			    createWeatherChart("Humidity", "left", 100, 0));
    pchart = new ApexCharts(document.querySelector("#pressure_series"),
			    createWeatherChart("Pressure", "left", undefined, 0));
    cchart = new ApexCharts(document.querySelector("#clouds_series"),
			    createWeatherChart("Cloud Coverage", "left", 100, 0));
    schart = new ApexCharts(document.querySelector("#sqm_series"),
			    createWeatherChart("Sky Quality", "left", undefined, 1));

    hchart.render();
    cchart.render();
    tchart.render();
    pchart.render();
    schart.render();

    tchart.updateOptions({colors: ["#008fec", "#0077b3"],
			  fill: {type: ['gradient', 'pattern'],
				 pattern: {style: 'verticalLines'}}});

    updateSeries();

    setInterval( function(){ updateSeries();}, 60000);
};

function updateSeries() {
    $.get("CHART/RTdata_6h.json", function(data) {

	hchart.updateSeries([data.Humidity]);
	cchart.updateSeries([data.CloudCover]);
	tchart.updateSeries([{data: data.Temperature.data,
			      name: data.Temperature.name,
			      type: "area"},
			     {data: data.DewPoint.data,
			      name: "Dew Point",
			      type: "area"}]);
	pchart.updateSeries([data.Pressure]);
	schart.updateSeries([data.SQM]);

	// update current value
	var currentTemperature   = data.Temperature.data[data.Temperature.data.length-1][1];
	var currentCloudCoverage = (data.CloudCover.data[data.CloudCover.data.length-1][1]).toFixed(0);
	var currentHumidity      = (data.Humidity.data[data.Humidity.data.length-1][1]).toFixed(0);
	var currentPressure      = (data.Pressure.data[data.Pressure.data.length-1][1]).toFixed(0);
	var currentSQM           = (data.SQM.data[data.SQM.data.length-1][1]).toFixed(settings.sqm_prec);

	// calculate filling percentage from current temperature and pressure (slightly ugly code)
	temperature.updateSeries([100 * (currentTemperature - settings.t_min) / (settings.t_max - settings.t_min)]);
	pressure.updateSeries([100 * (currentPressure - settings.p_min) / (settings.p_max - settings.p_min)]);
	cloudCoverage.updateSeries([currentCloudCoverage]);
	humidity.updateSeries([{name: "Humidity", data: [currentHumidity]}]);
	document.querySelector("#humidityValue").textContent = currentHumidity + "%";

	document.querySelector("#sqm").textContent = currentSQM;

	// update time stamp at the bottom line
	var lastUpdate = data.Temperature.data[data.Temperature.data.length-1][0];
	document.querySelector("#lastupdate").textContent = new Date(lastUpdate).toLocaleString();

    });

};
