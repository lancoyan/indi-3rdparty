/*  Streaming functions for the Davis anemometer measuring wind speed and
    direction:
    https://www.davisinstruments.com/product/anemometer-for-vantage-pro2-vantage-pro/

    Developed on basis of the hookup guide from
    http://cactus.io/hookups/weather/anemometer

    Copyright (C) 2020 Wolfgang Reissenberger <sterne-jaeger@t-online.de>

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
*/
#include <math.h>

#define WINDSPEEDPIN (2) // The pin location of the wind speed sensor

#define SLICEDURATION 5000 // interval for a single speed mesure

struct {
  bool status;
  float direction;
  unsigned int rotations;
  float avgSpeed;
  float minSpeed;
  float maxSpeed;
} anemometerData;

struct speedRawData {
  unsigned int rotations;   // Cup rotation counter used in interrupt routine
  unsigned long startTime;  // Start time of the measurement
} singleInterval;

volatile unsigned long startTime;     // overall start time for calculating the wind speed
volatile unsigned long startSlice;    // start time of the current time slice to measure wind speed
volatile unsigned long lastInterrupt; // Last time a rotation has been detected
volatile unsigned int rotations;      // total number of wind wheel rotations
volatile unsigned int sliceRotations; // rotation occured in the current time slice
volatile float minSpeed;              // minimal wind speed since startTime
volatile float maxSpeed;              // maximal wind speed since startTime


// calculate the windspeed
float windspeed(unsigned long time, unsigned long startTime, unsigned int rotations) {

  // 1600 rotations per hour or 2.25 seconds per rotation
  // equals 1 mp/h wind speed (1 mp/h = 1609/3600 m/s)
  // speed (m/s) = rotations * 1135.24 / delta t

  return (rotations * 1135.24 / (time - startTime));
}


// This is the function that the interrupt calls to increment the rotation count
void isr_rotation () {

  volatile unsigned long now = millis();
  if ((now - lastInterrupt) > 15 ) { // debounce the switch contact.
    rotations++;
    sliceRotations++;
    lastInterrupt = now;

    if (lastInterrupt - startSlice >= SLICEDURATION) {
      volatile float speed = windspeed(lastInterrupt, startSlice, sliceRotations);

      // update min and max values
      if (speed > maxSpeed)
        maxSpeed = speed;
      if (speed < minSpeed)
        minSpeed = speed;

      // reset the single interval
      startSlice = now;
      sliceRotations = 0;
    }
  }
}


void reset(unsigned long time) {
  startTime      = time;
  startSlice     = time;
  lastInterrupt  = time;
  rotations      = 0;
  sliceRotations = 0;
  maxSpeed       = 0.0;
  minSpeed       = 9999.0;
}


void initAnemometer() {
  pinMode(WINDSPEEDPIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(WINDSPEEDPIN), isr_rotation, FALLING);
  anemometerData.status = true;
  // reset measuring data
  reset(millis());
}

void updateAnemometer() {

  if (anemometerData.status) {
    // stop recording
    detachInterrupt(digitalPinToInterrupt(WINDSPEEDPIN));
    anemometerData.avgSpeed = windspeed(lastInterrupt, startTime, rotations);
    anemometerData.minSpeed = minSpeed < anemometerData.avgSpeed ? minSpeed : anemometerData.avgSpeed;;
    anemometerData.maxSpeed = maxSpeed > anemometerData.avgSpeed ? maxSpeed : anemometerData.avgSpeed;
    anemometerData.rotations = rotations;
    reset(millis());
    // start recording
    attachInterrupt(digitalPinToInterrupt(WINDSPEEDPIN), isr_rotation, FALLING);
  } else
    initAnemometer();
}
void serializeAnemometer(JsonDocument &doc) {

  JsonObject data = doc.createNestedObject("Davis Anemometer");
  data["init"] = anemometerData.status;

  if (anemometerData.status) {
    // data["direction"] = anemometerData.direction;
    data["avg speed"] = anemometerData.avgSpeed;
    data["min speed"] = anemometerData.minSpeed;
    data["max speed"] = anemometerData.maxSpeed;
    data["rotations"] = anemometerData.rotations;
  }
}
