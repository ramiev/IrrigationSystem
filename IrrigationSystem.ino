#include <CurieTime.h>
#include <CurieBLE.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <SPI.h>
#include <TFT.h>  // Arduino LCD library
#include <SD.h>

// Irrigation system and climate for plants home use
// by Rami Even-Tsur

/*
  components:
    Solenoid Valve ZE-4F180 NC DC 12V
    Soil Moisture Sensor YL-69
    Light Intensity Sensor Module 5528 Photo Resistor

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
*/

BLEService irrigationSysService("19B10000-E8F2-537E-4F6C-D104768A1214"); // BLE Irrigation system Service
// BLE Valve Switch Characteristic - custom 128-bit UUID, read and writable by central
BLEUnsignedCharCharacteristic switchCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
// BLE Set time Characteristic - custom 128-bit UUID, read and writable by central
BLEUnsignedLongCharacteristic setTimeCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);
// BLE sensors reading Characteristic - custom 128-bit UUID, read and writable by central
BLEIntCharacteristic readMoistCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// pin definition for sensors
#define moistureSensorPin A0 // input from moisture sensor
#define lightSensorPin A1    // input from Light sensor
#define Valve_Output1_PIN 7 // select the pin for the solenoid valve control

// pin definition for the tft display
#define cs   10
#define dc   9
#define rst  8

/*
  The circuit:
  SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 4 (for MKRZero SD: SDCARD_SS_PIN)
*/
// pin definition for the SD card
#define chipSelect 4

// create an instance of the library
TFT TFTscreen = TFT(cs, dc, rst);

// Data wire is plugged into port (pin) 2 on the Arduino
#define ONE_WIRE_BUS 2
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
int numberOfDevices; // Number of temperature devices found
DeviceAddress tempDeviceAddress; // We'll use this variable to store a found device address

int moistureSensorVal = 0; // initial variable to store the value coming from the sensor
int oldMoistureSensorVal = 1;
int lightSensorVal = 0;    // initial variable to store the value coming from the sensor
int oldLightSensorVal = 1;
float tempC = 0;
float oldTempC = 1;

//int hourVar = 12, minuteVar = 0, secondVar = 0, dayVar = 2, monthVar = 8, yearVar = 2019;
unsigned long startTime = 0, stopTime = 0, elapseTime = 0;
boolean ValveOutput1Stat = false;

void setup() {
  // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
  while (!Serial) {
    // wait for serial port to connect. Needed for native USB port only
    delay (5000);
    break;
  }


  setupSDcard();
  setupDisplay();
  bleSetup();
  oneWireSetup();
  // declare the inputs and outputs:
  pinMode(moistureSensorPin, INPUT);
  pinMode(lightSensorPin, INPUT);
  pinMode(Valve_Output1_PIN, OUTPUT);

  // set the current time hour, minute, second, day, month and year
  // setTime(hourVar, minuteVar, secondVar, dayVar, monthVar, yearVar);
  setTime(0);
  startTime = now();
  stopTime = now();
  elapseTime = 0;
  printTime();
}

void loop() {

  getMoisture();
  getLightState();
  getTemperatures();

  printDisplay("humidity " + String(moistureSensorVal), 0, 10);
  printDisplay("light " + String(lightSensorVal), 0, 30);
  printDisplay("temp " + String(tempC), 0, 50);
  if (ValveOutput1Stat ) {
    printDisplay("watering", 0, 70);
  } else {
    printDisplay("valve off" , 0, 70);
    printDisplay("watering was " + String(elapseTime), 0, 90);
  }
  printDisplay(printTime(), 0, 110);
  delay(1000);
  TFTscreen.fillScreen(0);

  waterMoistMode();
  // manualMode(true);

  writeDataToSDcard();

  // poll for BLE events
  BLE.poll();
}


void setupSDcard() {

  Serial.print("Initializing SD card...");

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    // while (1);
    delay (5000);
  }
  else  {
    Serial.println("card initialized.");
  }
}

void setupDisplay() {

  char printout[30];

  // Put this line at the beginning of every sketch that uses the GLCD:
  TFTscreen.begin();
  // clear the screen with a black background
  TFTscreen.background(0, 0, 0);

  // write the static text to the screen
  // set the font color to white
  TFTscreen.stroke(255, 255, 255);
  // set the font size
  TFTscreen.setTextSize(2);

  // clear screen
  TFTscreen.fillScreen(0);

  // write the text to the top left corner of the screen
  String headLineStr = String( "Rami's\n Irrigation\n system\n");
  headLineStr.toCharArray(printout, 30);

  TFTscreen.text(printout, 0, 10);

  // set the font size very large for the loop
  TFTscreen.setTextSize(1);

}

void printDisplay(String PrintOut, int xPos, int yPos) {
  // char array to print to the screen
  char sensorPrintout[20];

  String sensorVal = PrintOut;

  // convert the reading to a char array
  sensorVal.toCharArray(sensorPrintout, 20);

  // set the font color
  TFTscreen.stroke(255, 255, 255);

  // print the sensor value
  TFTscreen.text(sensorPrintout, xPos, yPos);

  // wait for a moment
  //delay(250);

  // erase the text you just wrote
  // TFTscreen.stroke(0, 0, 0);
  // TFTscreen.text(sensorPrintout, xPos, yPos);


}

void timeMode(long wateringTime, long wateringTimeDuration) {

}

void manualMode(boolean ofOnSwitch) {

  switch (ofOnSwitch) {
    case true : {
        digitalWrite(Valve_Output1_PIN, HIGH);
        ValveOutput1Stat = true;
        Serial.print("Valve is on, watering starts...");
        printTime();
        startTime = now();
        stopTime = now();
        elapseTime = stopTime - startTime;
        Serial.println();
      }
    case false : {
        digitalWrite(Valve_Output1_PIN, LOW);
        ValveOutput1Stat = false;
        Serial.print("Valve is off, watering Stops...");
        printTime();
        stopTime = now();
        elapseTime = stopTime - startTime;
        Serial.print("watering elapsed time is (sec): ");
        Serial.println(elapseTime);
        Serial.println();
      }
  }
}

void waterMoistMode() {

  // Evaluate the moisture sensor and light values and turn on valves as necessary

  if ((moistureSensorVal >= 1000) && (ValveOutput1Stat == false)) {
    digitalWrite(Valve_Output1_PIN, HIGH);
    ValveOutput1Stat = true;
    Serial.print("Valve is on, watering starts...");
    printTime();
    startTime = now();
    stopTime = now();
    elapseTime = stopTime - startTime;
    Serial.println();
  }

  if ((moistureSensorVal < 900) && (ValveOutput1Stat == true)) {
    digitalWrite(Valve_Output1_PIN, LOW);
    ValveOutput1Stat = false;
    Serial.print("Valve is off, watering Stops...");
    printTime();
    stopTime = now();
    elapseTime = stopTime - startTime;
    Serial.print("watering elapsed time is (sec): ");
    Serial.println(elapseTime);
    Serial.println();
  }
}

int getMoisture() {

  moistureSensorVal = analogRead(moistureSensorPin);
  if (oldMoistureSensorVal != moistureSensorVal) {
    oldMoistureSensorVal = moistureSensorVal;
    if (moistureSensorVal >= 1000  ) {
      Serial.print("Moisture reading (1000 ~1023 dry soil): ");
    }
    if ((moistureSensorVal >= 901) &&  (moistureSensorVal <= 999) ) {
      Serial.print("Moisture reading (901 ~999 humid soil): ");
    }
    if ((moistureSensorVal >= 0) &&  (moistureSensorVal <= 900) ) {
      Serial.print("Moisture reading (0 ~900 in water): ");
    }
    Serial.println(moistureSensorVal);
  }


  return moistureSensorVal;

}

boolean getLightState()  {
  boolean LightState = false;


  lightSensorVal = analogRead(lightSensorPin);
  if (lightSensorVal != oldLightSensorVal) {
    oldLightSensorVal = lightSensorVal;
    if ((lightSensorVal >= 0) && (lightSensorVal <= 214) ) {
      Serial.print("Light reading (0 - 214 no Light): ");
      LightState = false;
    }
    if ((lightSensorVal >= 215) && (lightSensorVal <= 1023 )) {
      Serial.print("Light reading (light 215 - 1023): ");
      LightState = true;
    }
    Serial.println(lightSensorVal);
  }

  return LightState;
}

float getTemperatures(void) {
  sensors.requestTemperatures(); // Send the command to get temperatures


  // Loop through each device, print out temperature data
  for (int i = 0; i < numberOfDevices; i++) {
    // Search the wire for address
    if (sensors.getAddress(tempDeviceAddress, i)) {

      // Output the device ID
      Serial.print("Temperature for device: ");
      Serial.println(i, DEC);

      // Print the data
      tempC = sensors.getTempC(tempDeviceAddress);
      if (oldTempC != tempC) {
        oldTempC = tempC;
        Serial.print("Temp C: ");
        Serial.print(tempC);
        Serial.print(" Temp F: ");
        Serial.println(DallasTemperature::toFahrenheit(tempC)); // Converts tempC to Fahrenheit
      }
    }
  }


  return tempC;
}

// tempeture setup
void oneWireSetup(void) {

  // Start up the library
  sensors.begin();

  // Grab a count of devices on the wire
  numberOfDevices = sensors.getDeviceCount();

  // locate devices on the bus
  Serial.print("Locating tempeture sensor devices...");
  Serial.print("Found ");
  Serial.print(numberOfDevices, DEC);
  Serial.println(" devices.");

  // Loop through each device, print out address
  for (int i = 0; i < numberOfDevices; i++) {
    // Search the wire for address
    if (sensors.getAddress(tempDeviceAddress, i)) {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }
  }
}


void bleSetup() {
  // begin initialization
  BLE.begin();

  // set advertised local name and service UUID:
  BLE.setLocalName("IrrigationSys");
  BLE.setAdvertisedService(irrigationSysService);

  // add the characteristic to the service
  irrigationSysService.addCharacteristic(switchCharacteristic);
  irrigationSysService.addCharacteristic(setTimeCharacteristic);
  irrigationSysService.addCharacteristic(readMoistCharacteristic);

  // add service
  BLE.addService(irrigationSysService);

  // assign event handlers for connected, disconnected to peripheral
  BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
  BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);

  // assign event handlers for characteristic
  switchCharacteristic.setEventHandler(BLEWritten, switchCharacteristicWritten);
  setTimeCharacteristic.setEventHandler(BLEWritten, setTimeCharacteristicWritten);
  // readMoistCharacteristic.setEventHandler(BLERead, readMoistCharacteristicRead);

  // set the initial value for the characeristic:
  switchCharacteristic.setValue(ValveOutput1Stat);
  //setTimeCharacteristic.setValue(now());

  // read moist value
  readMoistCharacteristic.setValue(moistureSensorVal);

  // start advertising
  BLE.advertise();

  Serial.println(("Bluetooth device active, waiting for connections..."));
}

void blePeripheralConnectHandler(BLEDevice central) {
  // central connected event handler
  Serial.print("Connected event, central: ");
  Serial.println(central.address());
}


void blePeripheralDisconnectHandler(BLEDevice central) {
  // central disconnected event handler
  Serial.print("Disconnected event, central: ");
  Serial.println(central.address());
}


void switchCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
  // central wrote new value to characteristic, update LED
  Serial.print("Characteristic event, written: ");

  if (switchCharacteristic.value()) {
    digitalWrite(Valve_Output1_PIN, HIGH);
    ValveOutput1Stat = true;
    Serial.print("Valve is on, watering starts...");
    printTime();
    startTime = now();
    Serial.println();
  } else {
    digitalWrite(Valve_Output1_PIN, LOW);
    ValveOutput1Stat = false;
    Serial.print("Valve is off, watering Stops...");
    printTime();
    stopTime = now();
    elapseTime = stopTime - startTime;
    Serial.print("watering elapsed time is (sec): ");
    Serial.println(elapseTime);
    Serial.println();
  }
}

void setTimeCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
  // central wrote new value to characteristic, update time
  Serial.print("Characteristic event, written: ");

  if (setTimeCharacteristic.value()) {
    setTime(setTimeCharacteristic.value());
    Serial.print("Time update done....the time is ");
    printTime();
  } else {
    Serial.print("Characteristic event, written: Time have no input");
  }
}

void readMoistCharacteristicRead(BLEDevice central, BLECharacteristic characteristic) {
  Serial.print("BLE moist reading");

}

String printTime() {
  String timeStr, dateStr;
  // Serial.print("Time now is: ");

  timeStr = print2digits(hour()) + ":" + print2digits(minute()) + ":" + print2digits(second());
  dateStr = String(day()) + "/" + String(month()) + "/" + String(year());
  String dateTimeStr = dateStr + " " + timeStr;
  Serial.print(dateTimeStr);
  Serial.println();
  return dateTimeStr;
}

String print2digits(int number) {
  String numStr;
  if (number >= 0 && number < 10) {
    numStr = '0' + number ;
  }
  else {
    numStr = number ;
  }
  return numStr;
}

// function to print a device address
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}


void writeDataToSDcard() {
  // make a string for assembling the data to log:
  String dataString = "";

  // read three sensors and append to the string:


  dataString = printTime()
               + "," +  String (tempC) + "℃"
               + "," +  String (moistureSensorVal) + " moisture"
               + "," +  String (lightSensorVal) + " light"
               + "," +  String (ValveOutput1Stat) + " Irrigation";


  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open("datalog1.txt", FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(dataString);
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening datalog.txt");
  }
}
