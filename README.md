# IrrigationSystem
  Irrigation system for plants home use
  by Rami Even-Tsur
  eventsur@gmail.com

  components:
    Arduino or Genuino 101
    Solenoid Valve ZE-4F180 NC DC 12V
    Soil Moisture Sensor YL-69
    Light Intensity Sensor Module 5528 Photo Resistor
    
    sdcard
    lcd display
    power supply and backup battery
    3 control buttons
    sensorButton (pin 3) - change sensore displying reading 
    modeButton (pin 5) - change opertion mode between Sensor Mode | Schedule Mode | Manual Mode | Set time Mode
    okButton (pin 6) - multi purpose button, create config.txt file | break irrigation opertion
    

  Moisture reading :
    1000 ~1023 dry soil
    901 ~999 humid soil
    0 ~900 in water

  Light reading :
    no Light 0 - 214
    light 214 - 1023

  Temperature Sensor :
    DS18B20 1-Wire Digital Temperature Sensor

  Measures soil moisture and lighting,
  watering plants when humidity is low and no sun through valve control.
  Measures and records irrigation time and moisture and lighting values
  
  The irrigation activity is documented and writing to file datalog.csv
  
  Configuration file is config.txt json file format :
  {"lastWateringDate":2273, // manualMode
  "sensorLastWateringDate":2273,
  "moistureWateringThreshhold":999,
  "lightWateringThreshhold":500,
  "wateringTime":300, // manualMode
  "schWateringTime":300,
  "schWateringFrequency":86400,
  "schLastWateringDate":2273}
  
  bluetooth LE support
  

  
  
