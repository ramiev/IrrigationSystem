/*
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
*/
#include <CurieTime.h>
#include <CurieBLE.h>
#include <ArduinoJson.h>  // version 6.16.1
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <TFT.h>  // Arduino LCD library
#include <SD.h>

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
#define SDchipSelect 4

// pin definition for buttons control
#define sensorButton 0
#define modeButton 5
#define okButton 6

// controls the pump motor speed using Pulse Width Modulation
// duty cycle: between 0 (always off) and 255 (always on).
// due to power supply limitation DutyCycleOn set to 160
#define pumpPwmPin 3
#define pumpPwmDutyCycleOn 255
#define pumpPwmDutyCycleOff 0

boolean serialOut = true; // use for printing lcd display output message to terminal for debug

// create an instance of TFT library
TFT TFTscreen = TFT(cs, dc, rst);

// Data wire is plugged into port (pin) 2 on the Arduino
#define ONE_WIRE_BUS 2
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
int numberOfDevices; // Number of temperature devices found
DeviceAddress tempDeviceAddress; // We'll use this variable to store a found device address

// irrigation timeing
unsigned long startTime = 0, stopTime = 0, elapseTime = 0;
unsigned long timeZone = 10800 ; // 10800 utc +3 hours in israel +2 7200

// watering Solenoid Valve stat
boolean ValveOutput1Stat = false;

// use for display clear old clock value
char clockPrintout[20] = "";

struct Config {
  unsigned long lastWateringDate;       // manual Mode
  unsigned long sensorLastWateringDate; // Sensor Mode
  int moistureWateringThreshhold;       // Sensor mode, moisture value threshhold
  int lightWateringThreshhold;          // Sensor mode, light value threshhold
  unsigned long wateringTime;           // Sensor mode, watering time and rest watering time in sec.
  unsigned long schWateringTime;        // schedule mode, long watering time in sec.
  unsigned long schLastWateringDate;    // schedule mode, Last Watering Date in sec.
  unsigned long schWateringFrequency;   // schedule mode, time between Watering in sec.
  float flowRate;                       // L/s
  float waterReservoirState;            // waterReservoirSize -  elapseTime * flowRate
  byte defaultMode;                     // set the default working mode
};

const char *configFileName = "/config.txt";  // <- SD library uses 8.3 filenames
Config config;                         // <- global configuration object

byte showSensorSelect = 0;
byte modeSelect = 0;
byte okSelect = 0;

BLEService irrigationService("19B10000"); // BLE Irrigation Service

// BLE Irrigation moisture light tempeture sensors level and ValveOutput1Stat Characteristic
// custom 128-bit UUID,
// remote clients will be able to get notifications if this characteristic changes
BLEUnsignedCharCharacteristic moistureCharacteristic("19B10001", BLERead | BLENotify);
BLEUnsignedCharCharacteristic lightCharacteristic("19B10002", BLERead | BLENotify);
BLEFloatCharacteristic tempetureCharacteristic("19B10003", BLERead | BLENotify);
BLECharCharacteristic ValveOutput1StatCharacteristic("19B10004", BLERead | BLENotify);


void setup() {
  // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
  while (!Serial) {
    // wait for serial port to connect. Needed for native USB port only
    delay (5000);
    break;
  }

  startTime = now();
  stopTime = now();

  setupDisplay();
  setupSDcard();
  oneWireSetup();

  // declare the inputs and outputs:
  pinMode(moistureSensorPin, INPUT);
  pinMode(lightSensorPin, INPUT);
  pinMode(Valve_Output1_PIN, OUTPUT);
  pinMode(pumpPwmPin, OUTPUT);

  // watering Off
  digitalWrite(Valve_Output1_PIN, LOW);         // stop the solenoid
  analogWrite(pumpPwmPin, pumpPwmDutyCycleOff); // stop the pump

  sdConfig();
  buttonSetup();
  bleSetup();

  modeSelect = config.defaultMode;
}


void loop() {
  clockDisplay(getStrTime(now()), 0, 110, 2);
  buttonMenu();
  bleConnect();
}


// bluetooth le setup
void bleSetup() {
  // begin initialization
  BLE.begin();

  // Set a local name for the BLE device
  // This name will appear in advertising packets
  // and can be used by remote devices to identify this BLE device
  // The name can be changed but maybe be truncated based on space left in advertisement packet
  BLE.setLocalName("Irrigation");
  // BLE.setDeviceName("IrrigationSys"); // GENUINO 101-E860
  // BLE.setAppearance(384);
  BLE.setAdvertisedService(irrigationService);  // add the service UUID

  // add the sensors level characteristic
  irrigationService.addCharacteristic(moistureCharacteristic);
  irrigationService.addCharacteristic(lightCharacteristic);
  irrigationService.addCharacteristic(tempetureCharacteristic);
  irrigationService.addCharacteristic(ValveOutput1StatCharacteristic);

  BLE.addService(irrigationService);   // Add the BLE irrigation service
  moistureCharacteristic.setValue(getMoisture()); // initial value for this characteristic
  lightCharacteristic.setValue(getLightValue()); // initial value for this characteristic
  tempetureCharacteristic.setValue(getTemperatures()); // initial value for this characteristic
  ValveOutput1StatCharacteristic.setValue(ValveOutput1Stat); // initial value for this characteristic

  // Start advertising BLE.  It will start continuously transmitting BLE
  // advertising packets and will be visible to remote BLE central devices
  // until it receives a new connection

  // start advertising
  BLE.advertise();

  Serial.println("Bluetooth device active, waiting for connections...");
}


void bleConnect() {
  // listen for BLE peripherals to connect:
  BLEDevice central = BLE.central();

  // if a central is connected to peripheral:
  if (central) {
    Serial.print("Connected to central: ");
    // print the central's MAC address:
    Serial.println(central.address());

    // while the central is still connected to peripheral:
    if (central.connected()) {
      // set the moisture value for the characeristic:
      moistureCharacteristic.setValue(getMoisture());
      lightCharacteristic.setValue(getLightValue());
      tempetureCharacteristic.setValue(getTemperatures());
      ValveOutput1StatCharacteristic.setValue(ValveOutput1Stat);

    } else  {
      // when the central disconnects, print it out:
      Serial.print(F("Disconnected from central: "));
      Serial.println(central.address());
    }

  }
}


void clockDisplay(String PrintOut, int xPos, int yPos, int fontSize) {
  // char array to print to the screen
  char sensorPrintout[20];
  String sensorVal = PrintOut;

  // erase the text you just wrote
  TFTscreen.stroke(0, 0, 0);
  TFTscreen.text(clockPrintout, xPos, yPos);

  // convert the reading to a char array
  sensorVal.toCharArray(sensorPrintout, 20);

  // set the font color
  TFTscreen.stroke(255, 255, 0);
  // set the font size very large for the loop
  TFTscreen.setTextSize(fontSize);
  // print the sensor value
  TFTscreen.text(sensorPrintout, xPos, yPos);
  // save current time for clear display and write the new time
  sensorVal.toCharArray(clockPrintout, 20);
}


void buttonMenu() {
  bool cls = false;
  bool clsTxt = false;

  // get control button stat
  bool sensorButtonVal = digitalRead(sensorButton);
  bool modeButtonVal = digitalRead(modeButton);
  bool okButtonVal = digitalRead(okButton);

  int moistureSensorVal = getMoisture();
  int lightSensorVal = getLightValue();
  float tempC = getTemperatures();

  serialCommandInterface();

  // in force show water state for filling indicator
  if (config.waterReservoirState <= 0) {
    showSensorSelect = 3;
    cls = false;
    clsTxt = true;
  }

  if ((sensorButtonVal == LOW) ) {
    showSensorSelect += 1;
    cls = true;
    clsTxt = true;
  }

  if ((modeButtonVal == LOW) ) {
    modeSelect += 1;
    cls = true;
    clsTxt = true;
    if (modeSelect > 3)  modeSelect = 0;
  }

  if ((okButtonVal == LOW) ) {
    okSelect += 1;
  }

  serialOut = false;
  if (ValveOutput1Stat) {
    sensorDisplay("Watering ON", 0, 55, 2, 2, 0, cls, false, serialOut);
  } else {
    sensorDisplay("Watering OFF", 0, 55, 2, 4, 0, cls, false, serialOut);
  }
  serialOut = true;

  // switch info between sensors display
  switch (showSensorSelect) {
    case 0:
      serialOut = false;
      if (cls)  {
        sensorDisplay("humidity\n" + String(moistureSensorVal) + " %", 0, 0, 3, 4, 0, cls, true, serialOut);
        cls = false;
      } else if (moistureSensorVal == getMoisture()) {
        sensorDisplay("humidity\n" + String(moistureSensorVal) + " %", 0, 0, 3, 4, 0, false, false, serialOut);
      } else if (moistureSensorVal != getMoisture()) {
        sensorDisplay("humidity\n" + String(moistureSensorVal) + " %", 0, 0, 3, 4, 0, true, true, serialOut);
      }
      serialOut = true;
      break;
    case 1:
      serialOut = false;
      if (cls) {
        sensorDisplay("light\n" + String(lightSensorVal) + " %", 0, 0, 3, 5, 0, cls, true, serialOut);
        cls = false;
      } else if (lightSensorVal == getLightValue()) {
        sensorDisplay("light\n" + String(lightSensorVal) + " %", 0, 0, 3, 5, 0, false, false, serialOut);
      } else if (lightSensorVal != getLightValue()) {
        sensorDisplay("light\n" + String(lightSensorVal) + " %", 0, 0, 3, 5, 0, true, false, serialOut);
      }
      serialOut = true;
      break;
    case 2:
      serialOut = false;
      if (cls) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, cls, true, serialOut);
        cls = false;
      } else if (tempC == getTemperatures()) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, false, false, serialOut);
      } else if (tempC != getTemperatures()) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, true, false, serialOut);
      }
      serialOut = true;
      break;
    case 3:
      serialOut = false;
      sensorDisplay("waterReservoirState\n" + String(config.waterReservoirState), 0, 0, 3, 6, 0, false, false, serialOut);
      serialOut = true;
      break;
    default:
      showSensorSelect = 0;
      break;
  }

  // switch working mode
  switch (modeSelect) {
    case 0: // sensor mode and reservoir refilled init
      serialOut = false;
      sensorDisplay("Sensor Mode", 0, 80, 2, 4, 0, cls, clsTxt, serialOut);
      serialOut = true;
      cls = false;
      sensorMode();
      tankRefilled(); //reservoir refilled init by ok button press
      break;
    case 1:
      serialOut = false;
      sensorDisplay("Schedule Mode", 0, 80, 2, 7, 0, cls, clsTxt, serialOut);
      serialOut = true;
      cls = false;
      scheduleMode();
      tankRefilled();  //reservoir refilled init by ok button press
      break;
    case 2:
      serialOut = false;
      sensorDisplay("Manual Mode", 0, 80, 2, 7, 0, cls, clsTxt, serialOut);
      serialOut = true;
      cls = false;
      manualMode();
      tankRefilled(); //reservoir refilled init by ok button press
      break;
    case 3:  // setup mode
      serialOut = false;
      sensorDisplay("Setup Mode", 0, 80, 2, 7, 0, cls, clsTxt, serialOut);
      serialOut = true;
      cls = false;
      if (okSelect) {  // load config from sd json config.txt
        sensorDisplay("Load config values from file", 0, 80, 2, 4, 1000, true, true, serialOut);
        loadConfiguration(configFileName, config);
        printConfigValues();
        // Dump Irrigation activity file to consol
        Serial.println("Print Irrigation activity file...");
        printFile("datalog.csv");
        okSelect = 0;
      }
      break;
    default:
      modeSelect = config.defaultMode;
      break;
  }
}


// Evaluate the moisture sensor and light values and turn on valves as necessary
void sensorMode() {
  int moistureSensorVal = getMoisture();
  int lightSensorVal = getLightValue();

  // moistureSensorVal 100% fully dry, 0% fully wet
  if ( (moistureSensorVal >= config.moistureWateringThreshhold) &&
       (lightSensorVal <= config.lightWateringThreshhold) &&
       (ValveOutput1Stat == false) &&
       ( (now() - config.sensorLastWateringDate) >= config.wateringTime) &&
       (config.waterReservoirState > 0) )
  {
    wateringOn();
    okSelect = 0;
    do {
      elapseTime = now() - startTime;
      moistureSensorVal = getMoisture();

      // read button state while watering for cancel
      int okButtonVal = digitalRead(okButton);
      if ((okButtonVal == LOW) ) {
        okSelect += 1;
      }

      float WaterReservoirState = getWaterReservoirState( getWateringVolume(elapseTime) );

      if ( (moistureSensorVal <= config.moistureWateringThreshhold)
           || (okSelect > 1)
           || (WaterReservoirState <= 0) )
      {
        okSelect = 0;
        sensorDisplay("Watering canceled", 0, 80, 2, 2, 1000, true, true, serialOut);
        break;
      }

      sensorDisplay("Sensor Mode", 0, 0, 1, 7, 0, false, false, serialOut);
      sensorDisplay("Moisture Thld " + String(config.moistureWateringThreshhold), 0, 15, 1, 7, 0, false, false, serialOut);
      sensorDisplay("Light Thld " + String(config.lightWateringThreshhold), 0, 30, 1, 7, 0, false, false, serialOut);
      sensorDisplay("wateringTime " + String(config.wateringTime), 0, 45, 1, 7, 0, false, false, serialOut);
      sensorDisplay("ElpsTime ", 0, 75, 2, 2, 0, false, false, serialOut);
      sensorDisplay("% Dry is " + String(getMoisture()), 0, 95, 1, 4, 0, false, false, serialOut);
      sensorDisplay(String(elapseTime) + " sec", 100, 75, 2, 2, 500, false, true, serialOut);
      clockDisplay(getStrTime(now()), 0, 110, 2);
      bleConnect();
    } while ( elapseTime <= config.wateringTime );
    wateringOff();
    config.sensorLastWateringDate = now();
    saveConfiguration(configFileName, config);
  }
}


// watering for preset time every periodic preset frequency
// schWateringTime, schWateringFrequency, schLastWateringDate saved to config file
void scheduleMode() {
  bool isRainy = rainIndicator();

  if ( (now() >= (config.schLastWateringDate + config.schWateringFrequency))
       && (ValveOutput1Stat == false)
       && (config.waterReservoirState > 0) )
  {
    okSelect = 0;
    wateringOn();
    do {
      elapseTime = now() - startTime;
      isRainy = rainIndicator();

      int okButtonVal = digitalRead(okButton);
      if ((okButtonVal == LOW) ) {
        okSelect += 1;
      }

      float WaterReservoirState = getWaterReservoirState( getWateringVolume(elapseTime) );

      // cancel watering when rainy or ok button press or water empty
      if ( (isRainy) || (okSelect > 1) || (WaterReservoirState <= 0) )
      {
        okSelect = 0;
        sensorDisplay("Watering canceled", 0, 80, 2, 2, 1000, true, true, serialOut);
        break;
      }
      sensorDisplay("Schedule Mode", 0, 0, 1, 7, 0, false, false, serialOut);
      sensorDisplay("LastWateringDate " + getStrTime(config.schLastWateringDate), 0, 15, 1, 7, 0, false, false, serialOut);
      sensorDisplay("WateringFrequency " + String(config.schWateringFrequency), 0, 30, 1, 7, 0, false, false, serialOut);
      sensorDisplay("wateringTime " + String(config.schWateringTime), 0, 45, 1, 7, 0, false, false, serialOut);
      sensorDisplay("ElpsTime ", 0, 75, 2, 2, 0, false, false, serialOut);
      sensorDisplay("% Dry is " + String(getMoisture()), 0, 95, 1, 4, 0, false, false, serialOut);
      sensorDisplay(String(elapseTime) + " sec", 100, 75, 2, 2, 1000, false, true, serialOut);
      clockDisplay(getStrTime(now()), 0, 110, 2);
      bleConnect();
    } while ( elapseTime <= config.schWateringTime );
    wateringOff();
    config.schLastWateringDate = now();
    saveConfiguration(configFileName, config);
  }
}


// watering for preset time long and returns to sensor mode
void manualMode() {
  bool isRainy = rainIndicator();

  if ( (ValveOutput1Stat == false)
       && (okSelect > 1)
       && (config.waterReservoirState > 0) )
  {
    okSelect = 0;
    wateringOn();
    do
    {
      elapseTime = now() - startTime;

      int okButtonVal = digitalRead(okButton);
      if ((okButtonVal == LOW) ) {
        okSelect += 1;
      }

      float WaterReservoirState = getWaterReservoirState( getWateringVolume(elapseTime) );
      // stop watering on Ok button true or moisture Threshhold or Water Reservoir empty
      if ( (isRainy) || (okSelect > 1) || (WaterReservoirState <= 0) )
      {
        okSelect = 0;
        sensorDisplay("Watering canceled", 0, 80, 2, 2, 1000, true, true, serialOut);
        break;
      }

      // sensorVal, xPos, yPos, fontSize, fontColorVal, delayVal, cls, clsTxt, serialOut
      sensorDisplay("Manual Mode", 0, 0, 1, 7, 0, false, false, serialOut);
      // sensorDisplay("Watering ON", 0, 55, 2, 2, 0, true, false, serialOut);
      sensorDisplay(String(WaterReservoirState) + " Liter remaining", 0, 75, 2, 2, 1000, false, true, serialOut);
      sensorDisplay(String(elapseTime) + " sec elapsed", 0, 90, 2, 2, 1000, false, true, serialOut);

      isRainy = rainIndicator();
      clockDisplay(getStrTime(now()), 0, 110, 2);
      bleConnect();
    } while (elapseTime <= config.wateringTime);
    wateringOff();
    config.lastWateringDate = now();
    saveConfiguration(configFileName, config);
    modeSelect = config.defaultMode; // return to sensor mode
  }
}


/* cli command
  log -> print log file to terminal
  log.del -> delete log file
  cfg -> print config info to terminal
  time.unixtime -> set time
  moisture.% -> sets moisture Watering Threshhold
*/
void serialCommandInterface() {
  String command = "" ; // for incoming serial data
  String param = "";

  // send data only when you receive data:
  while (Serial.available() > 0) {
    // read the incoming byte:
    command = Serial.readStringUntil('\n');

    param = command.substring(command.indexOf('.') + 1);
    command = command.substring(0, command.indexOf('.'));
    // Serial.println(command);
    // Serial.println(param);

    if ( command.equals("log") ) {
      if (param.equals("del")) {
        delFile("datalog.csv");
      }
      // Dump Irrigation activity file to consol
      Serial.println("Print Irrigation activity file...");
      printFile("datalog.csv");
    } else if (command.equals("cfg")) {
      Serial.println(command);
      printConfigValues();
    } else if (command.equals("time")) {
      cliSetTime(param);
    } else if (command.equals("moisture")) {
      cliSetMoistureTh(param);
    } else  {
      Serial.println("Unknown command");
    }
  }
}


void cliSetMoistureTh(String moistureThreshhold) {

  if ( ! ((moistureThreshhold.equals("moisture"))
          || (moistureThreshhold.equals("moisture."))) )
  {
    config.moistureWateringThreshhold = moistureThreshhold.toInt();
    saveConfiguration(configFileName, config);
    Serial.println("moisture Watering Threshhold is set");
  }
  Serial.println("moisture is " + String(config.moistureWateringThreshhold) );
}


// set system time by sending utc unix time number through serial
// unix time url https://www.unixtimestamp.com/index.php
void cliSetTime(String unixTimeStamp) {
  unsigned long timeZone = 10800 ; // 10800 utc +3 hours in israel +2 7200

  if ( ! (unixTimeStamp.equals("time")) ) {
    setTime(unixTimeStamp.toInt() + timeZone);
    serialOut = true;
    sensorDisplay("time is set", 0, 80, 2, 1, 500, true, true, serialOut);
    serialOut = false;
  }
  Serial.println(getStrTime(now()));
}


int getMoisture() {
  int moistureSensorVal = analogRead(moistureSensorPin);
  return map(moistureSensorVal, 0, 1023, 0, 100);
}


int  getLightValue()  {
  int lightSensorVal = analogRead(lightSensorPin);
  return map(lightSensorVal, 0, 1023, 0, 100);
}


float getTemperatures(void) {
  float tempC; // Temperature
  sensors.requestTemperatures(); // Send the command to get temperatures
  /*
    // Loop through each device, print out temperature data
    for (int i = 0; i < numberOfDevices; i++) {
      // Search the wire for address
      if (sensors.getAddress(tempDeviceAddress, i)) {
        // Output the device ID
        Serial.print("Temperature for device: ");
        Serial.println(i, DEC);
        // Print the data
        tempC = sensors.getTempC(tempDeviceAddress);
      }
    }
  */
  tempC = sensors.getTempC(tempDeviceAddress);
  return tempC;
}


String getStrTime(unsigned long unixTime) {
  String timeStr, dateStr;

  timeStr = print2digits(hour(unixTime)) + ":" + print2digits(minute(unixTime)) + ":" + print2digits(second(unixTime));
  dateStr = String(day(unixTime)) + "/" + String(month(unixTime)) + "/" + String(year(unixTime));
  String dateTimeStr = timeStr + " " + dateStr;
  return dateTimeStr;
}


String print2digits(int number) {
  String numStr;
  if (number >= 0 && number < 10) {
    numStr = "0" + String(number) ;
  }
  else {
    numStr = String(number) ;
  }
  return numStr;
}


void writeDataToSDcard() {
  // make a string for assembling the data to log:
  String dataString = "";
  // read three sensors and append to the string:
  dataString = getStrTime(now())
               + "," + String(getTemperatures()) + "℃"
               + "," + String(getMoisture()) + "% moisture"
               + "," + String(getLightValue()) + "% light"
               + "," + getStrTime(startTime)
               + "," + getStrTime(stopTime)
               + "," + String(stopTime - startTime) + " sec"
               + "," + String(modeSelect) + " mode";

  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open("datalog.csv", FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(dataString);
  }
  else { // if the file isn't open, pop up an error:
    Serial.println("error opening datalog.csv file !");
    setupSDcard();
  }
}


// sensorVal, xPos, yPos, fontSize, fontColorVal, delayVal, cls, clsTxt, serialOut
// print text on display
void sensorDisplay(String  sensorVal, int xPos, int yPos, int fontSize, int fontColorVal,
                   int delayVal, bool cls, bool clsTxt, bool serialOut) {
  // char array to print to the screen
  char sensorPrintout[30];
  byte colorCode[8][3] =
  { // (red, green, blue);
    {0, 0, 0}, //Black 0
    {0, 0, 255}, //Blue 1
    {255, 0, 0}, //Red 2
    {255, 0, 255}, //Magenta    3
    {0, 255, 0}, //Green 4
    {0, 255, 255}, //Cyan 5
    {255, 255, 0}, //Yellow 6
    {255, 255, 255} //White 7
  };

  // clear the screen with a black background
  if (cls) {
    TFTscreen.background(0, 0, 0);
  }

  // convert the reading to a char array
  sensorVal.toCharArray(sensorPrintout, 30);

  // set the font color
  TFTscreen.stroke(colorCode[fontColorVal][2], colorCode[fontColorVal][1], colorCode[fontColorVal][0]);

  // set the font size very large for the loop
  TFTscreen.setTextSize(fontSize);

  // print the sensor value
  TFTscreen.text(sensorPrintout, xPos, yPos);
  if (serialOut) Serial.println(sensorPrintout);

  // wait for a moment
  delay(delayVal);

  // erase the text you just wrote
  if (clsTxt) {
    TFTscreen.stroke(0, 0, 0);
    TFTscreen.text(sensorPrintout, xPos, yPos);
  }
}


void wateringOn() {
  digitalWrite(Valve_Output1_PIN, HIGH);
  analogWrite(pumpPwmPin, pumpPwmDutyCycleOn); // run the pump
  ValveOutput1Stat = true;
  startTime = now();
  if (ValveOutput1Stat) {
    sensorDisplay("Watering ON", 0, 55, 2, 2, 0, true, false, serialOut);
  } else {
    sensorDisplay("Watering OFF", 0, 55, 2, 4, 0, true, false, serialOut);
  }
}


void wateringOff() {
  digitalWrite(Valve_Output1_PIN, LOW);         // stop the solenoid
  analogWrite(pumpPwmPin, pumpPwmDutyCycleOff); // stop the pump
  ValveOutput1Stat = false;
  stopTime = now();
  if (ValveOutput1Stat) {
    sensorDisplay("Watering ON", 0, 55, 2, 2, 0, true, false, serialOut);
  } else {
    sensorDisplay("Watering OFF", 0, 55, 2, 4, 0, true, false, serialOut);
  }
  writeDataToSDcard();
  config.waterReservoirState = getWaterReservoirState( getWateringVolume(elapseTime) );
}


/*
  fluid flow rate = area of the pipe or channel�velocity of the liquid
  Q = Av
  Q = liquid flow rate (m3/s or L/s) liters per second
  A = area of the pipe or channel (m2) area is A = πr2
  v = velocity of the liquid (m/s)
*/
float getWateringVolume(unsigned long wateringElapseTime) {
  float wateringVolume = 0;
  wateringVolume = wateringElapseTime * config.flowRate;
  return wateringVolume;
}


float getWaterReservoirState(float wateringVolume) {
  float waterInReservoir = 0;
  waterInReservoir  = config.waterReservoirState - wateringVolume;
  return waterInReservoir;
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
  // TFTscreen.text(printout, 0, 10);
  // set the font size very large for the loop
  TFTscreen.setTextSize(1);
  sensorDisplay(headLineStr, 0, 0, 1, 7, 0, false, true, serialOut);
}


void setupSDcard() {
  sensorDisplay("Initializing SD card...", 0, 0, 1, 7, 0, false, true, serialOut);
  // see if the card is present and can be initialized:
  if (!SD.begin(SDchipSelect)) {
    sensorDisplay("Card failed, or not present", 0, 10, 1, 7, 0, false, true, serialOut);
    // don't do anything more:
    // while (1);
    loadDefaultConfigValues();
    delay (5000);

  }
  else  {
    sensorDisplay("card initialized.", 0, 10, 1, 7, 0, false, true, serialOut);
  }
}


// tempeture setup
// device 0 with address: 28FFAFD3741604CB
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


// function to print a device address
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}


// json file config.txt format
void sdConfig() {
  // Should load default config if run for the first time
  sensorDisplay("Loading configuration...", 0, 55, 2, 2, 0, true, true, serialOut);
  loadConfiguration(configFileName, config);
  // Dump config file
  sensorDisplay("Print config file...", 0, 55, 2, 2, 0, true, true, serialOut);
  printFile(configFileName);
  printConfigValues();
}


// Loads the configuration from a file
void loadConfiguration(const char *filename, Config & config) {
  // Open file for reading
  File file = SD.open(filename);
  if (!file) {
    sensorDisplay("reading file fail, check sd card !", 0, 55, 2, 2, 0, true, true, serialOut);
    Serial.println(filename);
    setupSDcard();
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<512> doc;
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    sensorDisplay("fail,goto default!", 0, 55, 2, 2, 0, true, true, serialOut);
    Serial.println(F("Failed to read file, using default configuration"));
    Serial.println(error.c_str());
    // Load default config values
    loadDefaultConfigValues();
    saveConfiguration(configFileName, config);
  } else {
    // Copy values from the JsonDocument to the Config
    config.lastWateringDate = doc["lastWateringDate"];
    config.sensorLastWateringDate = doc["sensorLastWateringDate"];
    config.moistureWateringThreshhold = doc["moistureWateringThreshhold"];
    config.lightWateringThreshhold = doc["lightWateringThreshhold"];
    config.wateringTime = doc["wateringTime"]; //sec
    config.schWateringTime  = doc["schWateringTime"];
    config.schWateringFrequency = doc["schWateringFrequency"]; // 1 day
    config.schLastWateringDate = doc["schLastWateringDate"];
    config.waterReservoirState = doc["waterReservoirState"]; //liters
    config.flowRate = doc["flowRate"]; // liters/sec 1/3600 240 L/H 1/120 0.01
    config.defaultMode = doc["defaultMode"];

    sensorDisplay("Config load complete", 0, 55, 2, 2, 0, true, true, serialOut);
  }
  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
}


// Saves the configuration to a file
void saveConfiguration(const char *filename, const Config & config) {
  // Delete existing file, otherwise the configuration is appended to the file
  SD.remove(filename);

  // Open file for writing
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create file Config.txt...Try again"));
    setupSDcard();
    return;
  }
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<512> doc;

  // Set the values in the document
  doc["lastWateringDate"] = config.lastWateringDate;
  doc["sensorLastWateringDate"] = config.sensorLastWateringDate;
  doc["moistureWateringThreshhold"] = config.moistureWateringThreshhold;
  doc["lightWateringThreshhold"] = config.lightWateringThreshhold;
  doc["wateringTime"] = config.wateringTime;

  doc["schWateringTime"] = config.schWateringTime;
  doc["schWateringFrequency"] = config.schWateringFrequency;
  doc["schLastWateringDate"] = config.schLastWateringDate;

  doc["waterReservoirState"] = config.waterReservoirState;
  doc["flowRate"] = config.flowRate;
  doc["defaultMode"] = config.defaultMode;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  // Close the file
  file.close();
}

// DEL file name
void delFile(const char *filename) {
  // Check to see if the file exists:
  Serial.println(filename);
  if (SD.exists(filename)) {
    Serial.println(" Exists.");
    // delete the file:
    Serial.println(" Removing...");
    SD.remove(filename);
  } else {
    Serial.println("Doesn't exist.");
  }
}


// Prints the content of a file to the Serial
void printFile(const char *filename) {

  if (SD.exists(filename)) {
    // Open file for reading
    File file = SD.open(filename);
    if (!file) {
      Serial.println(F("Failed to read file"));
      Serial.println(F(filename));
      Serial.println(F("Try again..."));
      setupSDcard();
      return;
    }
    // Extract each characters by one by one
    while (file.available()) {
      Serial.print((char)file.read());
    }
    Serial.println();
    // Close the file
    file.close();
  } else {
    Serial.println(F(filename));
    Serial.println(" Doesn't exist.");
  }
}


void buttonSetup() {
  //configure pin 3 5 6 as an input and enable the internal pull-up resistor
  pinMode(sensorButton, INPUT_PULLUP);
  pinMode(modeButton, INPUT_PULLUP);
  pinMode(okButton, INPUT_PULLUP);
}


void loadDefaultConfigValues() {
  // load default config values
  config.moistureWateringThreshhold = 25; // 100% fully dry , 0% fully wet
  config.lightWateringThreshhold = 100;  // 100% fully light, 0% fully dark
  config.wateringTime = 60 ;             // manual and sensor mode Duration of watering in seconds
  config.lastWateringDate = now() + timeZone;       // manual mode date of last time watering
  config.sensorLastWateringDate = now() + timeZone; // sensor mode date of last time watering
  // sch. mode
  config.schLastWateringDate = now() + timeZone;   // date of last time watering
  config.schWateringTime = 180;          // Duration of watering in seconds
  config.schWateringFrequency = 86400;  // Watering frequency in seconds, day is 86400 sec
  config.waterReservoirState = 3.3;   // 3.70 liters
  config.flowRate = 0.01;           // liters/sec 1/3600 0.016666667
  config.defaultMode = 1;           // Sensor Mode 0 ,Schedule Mode 1 ,Manual Mode 2
  sensorDisplay("Load complete Default Values !", 0, 55, 2, 2, 0, true, true, serialOut);
}


// logic in case of water reservoir refilled to the full
void tankRefilled() {
  if (okSelect && showSensorSelect == 3) {
    config.waterReservoirState = 3.3;
    saveConfiguration(configFileName, config);
    sensorDisplay("Reservoir refilled", 0, 80, 2, 4, 1000, true, true, serialOut);
    okSelect = 0;
  }
}


void printConfigValues() {

  Serial.println("=====Print Loaded Config Values=====");
  Serial.print("lastWateringDate ");
  Serial.println( getStrTime(config.lastWateringDate) );
  Serial.print("sensorLastWateringDate ");
  Serial.println( getStrTime(config.sensorLastWateringDate) );
  Serial.print("moistureWateringThreshhold ");
  Serial.println(config.moistureWateringThreshhold);
  Serial.print("lightWateringThreshhold ");
  Serial.println(config.lightWateringThreshhold);
  Serial.print("wateringTime ");
  Serial.println(config.wateringTime);
  Serial.print("schWateringTime ");
  Serial.println(config.schWateringTime);
  Serial.print("schWateringFrequency ");
  Serial.println(config.schWateringFrequency);
  Serial.print("schLastWateringDate ");
  Serial.println( getStrTime(config.schLastWateringDate) );
  Serial.print("waterReservoirState ");
  Serial.println(config.waterReservoirState);
  Serial.print("flowRate ");
  Serial.println(config.flowRate);
  Serial.print("defaultMode ");
  Serial.println(config.defaultMode);

  Serial.println();
}


// uses moisture Sensor for indicat rain (return true) to alow stop watering.
// moistureSensorVal 100% fully dry, 0% fully wet
bool rainIndicator() {
  int moistureSensorVal;
  moistureSensorVal = getMoisture();
  if (moistureSensorVal >= 20) return false;
  return true;
}
