function createWeatherChart() {

    var chart = {
	height: 250,
	type: "area",
	toolbar: {show: false},
    };
    var xaxis = {
	type: "datetime",
	labels: {style: {colors: '#ccc'}}
    };
    return {chart: chart,
	    xaxis: xaxis,
	    series: [],
	    stroke: {curve: 'smooth', width: 2},
	    legend: {labels: {colors: ['#ccc']}, showForSingleSeries: true},
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

var currentTimeline = "6h";

function selectTimeline(activeElement) {
    var els = document.querySelectorAll("button");
    Array.prototype.forEach.call(els, function (el) {
        el.classList.remove('active');
    });

    activeElement.target.classList.add('active');

    // extract timeline from button ID
    var id = activeElement.target.id;
    if (id.indexOf("timeline_") >= 0) {
	timeline = id.substring(9, id.length);
	updateSeries(timeline);
	currentTimeline = timeline;
    }
}


var tChart, cchart, pchart;
var temperature, humidity, pressure, cloudCoverage, sqm;

var settings = {t_min: -40, t_max: 50, t_prec: 1,
		p_min: 973, p_max: 1053, p_prec: 0,
		sqm_min: 0, sqm_max: 25, sqm_prec: 1};

function init() {

    // add event listeners to buttons
    document.querySelector("#timeline_6h").
	addEventListener('click', function (e) {selectTimeline(e);});
    document.querySelector("#timeline_1d").
	addEventListener('click', function (e) {selectTimeline(e);});
    document.querySelector("#timeline_7d").
	addEventListener('click', function (e) {selectTimeline(e);});
    document.querySelector("#timeline_30d").
	addEventListener('click', function (e) {selectTimeline(e);});


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
    sqm = new ApexCharts(document.querySelector("#sqm"),
			      createBarChart('SQM', '%', settings.sqm_min, settings.sqm_max));
    temperature.render();
    humidity.render();
    pressure.render();
    cloudCoverage.render();
    sqm.render();

    // special case for temperature
    temperature.updateOptions({
    }, true, false, false);
    
    // create the time series charts
    
    tChart = new ApexCharts(document.querySelector("#temperature_series"),
			      createWeatherChart());
    pchart = new ApexCharts(document.querySelector("#pressure_series"),
			    createWeatherChart());
    cchart = new ApexCharts(document.querySelector("#clouds_series"),
			      createWeatherChart());

    tChart.render();
    pchart.render();
    cchart.render();

    tChart.updateOptions({colors: ["#008fec", "#0077b3", '#9933ff'],
			    fill: {type: ['gradient', 'pattern', 'gradient'],
				   pattern: {style: 'verticalLines'}},
			    yaxis: [{seriesName: 'Temperature',
				     labels: {style: {color: '#ccc'}},
				     decimalsInFloat: 1,
				     title: {text: '°C',
					     style: {color: '#ccc'}}},
				    {seriesName: 'Temperature', show: false},
				    {seriesName: 'Percent',
				     opposite: true,
				     labels: {style: {color: '#ccc'}},
				     decimalsInFloat: 0,
				     max: 100,
				     title: {text: '%',
					     style: {color: '#ccc'}}}
				   ]});

    pchart.updateOptions({yaxis: {labels: {style: {color: '#ccc'}},
				  decimalsInFloat: 0,
				  title: {text: 'hPa',
					  style: {color: '#ccc'}}}});
    cchart.updateOptions({colors: ["#008fec", "#9933ff"],
			  fill: {type: ['gradient', 'gradient']},
			  yaxis: [{seriesName: 'Percent',
				   labels: {style: {color: '#ccc'}},
				   decimalsInFloat: 0,
				   max: 100,
				   title: {text: '%',
					   style: {color: '#ccc'}}},
				  {seriesName: 'SQM',
				   opposite: true,
				   labels: {style: {color: '#ccc'}},
				   decimalsInFloat: 1,
				   title: {text: 'mag/arcsec²',
					   style: {color: '#ccc'}}}
				 ]});

    updateSeries(currentTimeline);

    // update timelines every 5 min
    setInterval( function(){ updateSeries(currentTimeline);}, 5*60000);
};

function updateSeries(timeline) {
    // update last values
    $.get("CHART/RTdata_lastupdate.json", function(data) {

	var currentTemperature   = data.Temperature;
	var currentCloudCoverage = data.CloudCover;
	var currentHumidity      = data.Humidity;
	var currentPressure      = data.Pressure;
	var currentSQM           = data.SQM;

	// calculate filling percentage from current temperature and pressure (slightly ugly code)
	temperature.updateSeries([100 * (currentTemperature - settings.t_min) / (settings.t_max - settings.t_min)]);
	pressure.updateSeries([100 * (currentPressure - settings.p_min) / (settings.p_max - settings.p_min)]);
	cloudCoverage.updateSeries([currentCloudCoverage]);
	humidity.updateSeries([{name: "Humidity", data: [currentHumidity]}]);
	document.querySelector("#humidityValue").textContent = currentHumidity + "%";
	sqm.updateSeries([{name: "SQM", data: [currentSQM]}]);
	document.querySelector("#sqmValue").textContent = currentSQM.toFixed(1);

	// update time stamp at the bottom line
	var lastUpdate = data.timestamp;
	document.querySelector("#lastupdate").textContent = new Date(lastUpdate).toLocaleString();
    });


    $.get("CHART/RTdata_" + timeline + ".json", function(data) {

	tChart.updateSeries([{data: data.Temperature.data,
			      name: data.Temperature.name,
			      type: "area"},
			     {data: data.DewPoint.data,
			      name: "Dew Point",
			      type: "area"},
			     {data: data.Humidity.data,
			      name: "Humidity",
			      type: "area"}]);
	pchart.updateSeries([data.Pressure]);
	cchart.updateSeries([{data: data.CloudCover.data,
			      name: "Cloud Cover",
			      type: "area"},
			     {data: data.SQM.data,
			      name: "Sky Quality",
			      type: "area"}]);
    });

};
