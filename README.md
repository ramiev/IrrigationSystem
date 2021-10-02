# IrrigationSystem
  Irrigation system for plants home use
  by Rami Even-Tsur
  eventsur@gmail.com

  components:
    Arduino or Genuino 101
    Solenoid Valve ZE-4F180 NC DC 12V
    Soil Moisture Sensor YL-69
    Light Intensity Sensor Module 5528 Photo Resistor
    1.8" Serial SPI 128x160 Color TFT LCD Module Display (Driver IC ST7735)
    DS18B20 1-Wire Digital Temperature Sensor
    QR30E DC 12V 4.2W 240L/H Flow Rate Waterproof Brushless Pump

  Moisture soil reading :
    1000 ~1023 dry soil
    901 ~999 humid soil
    0 ~900 in water

  Light reading :
    no Light 0 - 214
    light 214 - 1023

  Measures soil moisture and lighting,
  watering plants when humidity is low and no sun through valve control.
  Measures and records irrigation time and moisture and lighting values to SD card
 
  bluetooth LE support

note:
 beacuse of low water pressor a water pump and pump circute add to the system.
 arduino pin 3 controls the pump motor speed using Pulse Width Modulation
 duty cycle: between 0 (always off) and 255 (always on).
 due to power supply limitation DutyCycleOn set to 160.
 sensorButton pin changed to 0.
