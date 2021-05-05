#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <TFT.h>  // Arduino LCD library
#include <SD.h>
#include <CurieTime.h>
#include <CurieBLE.h>

/*
  Irrigation system for plants home use
  by Rami Even-Tsur
  eventsur@gmail.com

  components:
    Arduino or Genuino 101
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

boolean serialOut = false; // use for printing lcd display output message to terminal for debug

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

// irrigation timeing
unsigned long startTime = 0, stopTime = 0, elapseTime = 0;

// watering Solenoid Valve stat
boolean ValveOutput1Stat = false;

// use for display clear old clock value
char clockPrintout[20] = "";

byte showSensorSelect = 0;
byte modeSelect = 0;
byte okSelect = 0;

struct Config {
  unsigned long lastWateringDate; // manual Mode
  unsigned long sensorLastWateringDate; // Sensor Mode
  int moistureWateringThreshhold; // Sensor mode, moisture value threshhold
  int lightWateringThreshhold; // Sensor mode, light value threshhold
  unsigned long wateringTime; // Sensor mode, watering time and rest watering time in sec.
  unsigned long schWateringTime; // schedule mode, long watering time in sec.
  unsigned long schLastWateringDate; // schedule mode, Last Watering Date in sec.
  unsigned long schWateringFrequency; // schedule mode, time between Watering in sec.
  float flowRate; // L/s 
  float waterReservoirState; // waterReservoirSize -  elapseTime * flowRate
};

const char *filename = "/config.txt";  // <- SD library uses 8.3 filenames
Config config;                         // <- global configuration object

BLEService irrigationService("19B10000"); // BLE Irrigation Service

// BLE Irrigation moisture light tempeture sensors level and ValveOutput1Stat Characteristic
// custom 128-bit UUID,
// remote clients will be able to get notifications if this characteristic changes
BLEUnsignedCharCharacteristic moistureCharacteristic("19B10001", BLERead | BLENotify);
BLEUnsignedCharCharacteristic lightCharacteristic("19B10002", BLERead | BLENotify);
BLEFloatCharacteristic tempetureCharacteristic("19B10003", BLERead | BLENotify);
BLECharCharacteristic ValveOutput1StatCharacteristic("19B10004", BLERead | BLENotify);


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

  sdConfig();
  buttonSetup();
  wateringOff();
  bleSetup();
}


void loop() {
  clockDisplay(getStrTime(now()), 0, 110, 2);
  buttonMenu();
  bleConnect();
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

  if ((sensorButtonVal == LOW) ) {
    showSensorSelect += 1;
    cls = true;
    clsTxt = true;
  }

  if ((modeButtonVal == LOW) ) {
    modeSelect += 1;
    cls = true;
    clsTxt = true;
  }

  if ((okButtonVal == LOW) ) {
    okSelect += 1;
  }

  if (ValveOutput1Stat) {
    sensorDisplay("Watering ON", 0, 55, 2, 2, 0, cls, false);
  } else {
    sensorDisplay("Watering OFF", 0, 55, 2, 4, 0, cls, false);
  }

  switch (showSensorSelect) {
    case 0:
      if (cls)  {
        sensorDisplay("humidity\n" + String(moistureSensorVal) + " %", 0, 0, 3, 4, 0, cls, true);
        cls = false;
      } else if (moistureSensorVal == getMoisture()) {
        sensorDisplay("humidity\n" + String(moistureSensorVal) + " %", 0, 0, 3, 4, 0, false, false);
      } else if (moistureSensorVal != getMoisture()) {
        sensorDisplay("humidity\n" + String(moistureSensorVal) + " %", 0, 0, 3, 4, 0, true, true);
      }
      break;
    case 1:
      if (cls) {
        sensorDisplay("light\n" + String(lightSensorVal) + " %", 0, 0, 3, 5, 0, cls, true);
        cls = false;
      } else if (lightSensorVal == getLightValue()) {
        sensorDisplay("light\n" + String(lightSensorVal) + " %", 0, 0, 3, 5, 0, false, false);
      } else if (lightSensorVal != getLightValue()) {
        sensorDisplay("light\n" + String(lightSensorVal) + " %", 0, 0, 3, 5, 0, true, false);
      }
      break;
    case 2:
      if (cls) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, cls, true);
        cls = false;
      } else if (tempC == getTemperatures()) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, false, false);
      } else if (tempC != getTemperatures()) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, true, false);
      }
      break;
    case 3:
      sensorDisplay("waterReservoirState\n" + String(config.waterReservoirState), 0, 0, 3, 6, 0, false, false);
      break;
    default:
      showSensorSelect = 0;
      //    sensorDisplay("", 0, 0, 3, 5, 0, true, true);
      break;
  }

  switch (modeSelect) {
    case 0:
      sensorDisplay("Sensor Mode", 0, 80, 2, 4, 0, cls, clsTxt);
      cls = false;
      sensorMode();
      if (okSelect) {
        // Dump Irrigation activity file
        Serial.println("Print Irrigation activity file...");
        printFile("datalog.csv");
        // inint config file to defaults
        setConfigValues();
        saveConfiguration(filename, config);
        sensorDisplay("init config", 0, 80, 2, 4, 1000, true, true);
        // Dump config file
        Serial.println(F("Print config file..."));
        printFile(filename);
        okSelect = 0;
      }
      break;
    case 1:
      sensorDisplay("Schedule Mode", 0, 80, 2, 7, 0, cls, clsTxt);
      cls = false;
      scheduleMode();
      break;
    case 2:
      sensorDisplay("Manual Mode", 0, 80, 2, 7, 0, cls, clsTxt);
      cls = false;
      manualMode();
      break;
    case 3:
      serialSetTime();
      sensorDisplay("Set time Mode", 0, 80, 2, 7, 0, cls, clsTxt);
      cls = false;
      break;
    default:
      modeSelect = 0;
      break;
  }

}


/* Evaluate the moisture sensor and light values and turn on valves as necessary
  Moisture reading :
    1000 ~1023 dry soil
    901 ~999 humid soil
    0 ~900 in water

  Light reading :
    no Light 0 - 214
    light 214 - 1023
*/
void sensorMode() {
  int moistureSensorVal = getMoisture();
  int lightSensorVal = getLightValue();

  if ((moistureSensorVal >= config.moistureWateringThreshhold) &&
      (lightSensorVal <= config.lightWateringThreshhold) && (ValveOutput1Stat == false) &&
      ( (now() - config.sensorLastWateringDate) >= config.wateringTime))
  {
    wateringOn();
    do {
      elapseTime = now() - startTime;
      moistureSensorVal = getMoisture();

      int okButtonVal = digitalRead(okButton);
      if ((okButtonVal == LOW) ) {
        okSelect += 1;
      }

      if ( ((moistureSensorVal <= config.moistureWateringThreshhold) && (config.wateringTime <= elapseTime)) || (okSelect > 1) ) {
        okSelect = 0;
        break;
      }

      sensorDisplay("Sensor Mode", 0, 0, 1, 7, 0, false, false);
      sensorDisplay("Moisture Thld " + String(config.moistureWateringThreshhold), 0, 15, 1, 7, 0, false, false);
      sensorDisplay("Light Thld " + String(config.lightWateringThreshhold), 0, 30, 1, 7, 0, false, false);
      sensorDisplay("wateringTime " + String(config.wateringTime), 0, 45, 1, 7, 0, false, false);
      sensorDisplay("ElpsTime ", 0, 75, 2, 2, 0, false, false);
      sensorDisplay("% Dry is " + String(getMoisture()), 0, 95, 1, 4, 0, false, false);
      sensorDisplay(String(elapseTime) + " sec", 100, 75, 2, 2, 500, false, true);
      clockDisplay(getStrTime(now()), 0, 110, 2);
      bleConnect();
    } while (elapseTime <= config.wateringTime);
    wateringOff();
    config.sensorLastWateringDate = now();
    saveConfiguration(filename, config);
  }
}


// watering for preset time every periodic preset frequency
// schWateringTime, schWateringFrequency, schLastWateringDate saved to config file
void scheduleMode() {
  int moistureSensorVal = getMoisture();

  if ( (now() >= (config.schLastWateringDate + config.schWateringFrequency)) &&
       (ValveOutput1Stat == false) )
  {
    wateringOn();
    do {
      elapseTime = now() - startTime;
      moistureSensorVal = getMoisture();

      int okButtonVal = digitalRead(okButton);
      if ((okButtonVal == LOW) ) {
        okSelect += 1;
      }

      // if moistureSensorVal --> 100 dry , 0 wet
      if ((moistureSensorVal <= config.moistureWateringThreshhold) || (okSelect > 1)) {
        // config.wateringTime = elapseTime;
        okSelect = 0;
        break;
      }
      sensorDisplay("Schedule Mode", 0, 0, 1, 7, 0, false, false);
      sensorDisplay("LastWateringDate " + getStrTime(config.schLastWateringDate), 0, 15, 1, 7, 0, false, false);
      sensorDisplay("WateringFrequency " + String(config.schWateringFrequency), 0, 30, 1, 7, 0, false, false);
      sensorDisplay("wateringTime " + String(config.schWateringTime), 0, 45, 1, 7, 0, false, false);
      sensorDisplay("ElpsTime ", 0, 75, 2, 2, 0, false, false);
      sensorDisplay("% Dry is " + String(getMoisture()), 0, 95, 1, 4, 0, false, false);
      sensorDisplay(String(elapseTime) + " sec", 100, 75, 2, 2, 1000, false, true);
      clockDisplay(getStrTime(now()), 0, 110, 2);
      bleConnect();
    } while (elapseTime <= config.schWateringTime);
    wateringOff();
    config.schLastWateringDate = now();
    saveConfiguration(filename, config);
  }
}


// watering for preset time long and returns to sensor mode
void manualMode() {
  int moistureSensorVal = getMoisture();

  if ( (ValveOutput1Stat == false) && (okSelect > 1))
  {
    wateringOn();
    do {
      elapseTime = now() - startTime;
      moistureSensorVal = getMoisture();
      sensorDisplay("Manual Mode", 0, 0, 1, 7, 0, false, false);
      sensorDisplay("LastWateringDate " + getStrTime(config.lastWateringDate), 0, 30, 1, 7, 0, false, false);
      sensorDisplay("wateringTime " + String(config.wateringTime), 0, 45, 1, 7, 0, false, false);
      sensorDisplay("ElpsTime ", 0, 75, 2, 2, 0, false, false);
      sensorDisplay(String(elapseTime) + " sec", 100, 75, 2, 2, 1000, false, true);
      sensorDisplay("% Dry is " + String(getMoisture()), 0, 95, 1, 4, 0, false, false);
      clockDisplay(getStrTime(now()), 0, 110, 2);
      bleConnect();
    } while (elapseTime <= config.wateringTime);
    wateringOff();
    config.lastWateringDate = now();
    saveConfiguration(filename, config);
    okSelect = 0 ;
    modeSelect = 0; // return to sensor mode
  }
}


// set system time by sending utc unix time number through serial
// unix time url https://www.unixtimestamp.com/index.php
void serialSetTime() {
  unsigned long timeZone = 10800 ; // 10800 utc +3 hours in israel +2 7200
  String incomingByte = "" ; // for incoming serial data
  //  Serial.println("https://www.unixtimestamp.com/index.php");
  //  Serial.println("Enter unix time in sec");

  // send data only when you receive data:
  while (Serial.available() > 0) {
    // read the incoming byte:
    char c = Serial.read();  //gets one byte from serial buffer
    incomingByte += c; //makes the string readString
    if (c == 10) {
      setTime(incomingByte.toInt() + timeZone);
      serialOut  = true;
      sensorDisplay("time is set", 0, 80, 2, 1, 500, true, true);
      serialOut = false;
      break;
    } else {
      Serial.println(c);
    }
  }
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
               //   + "," + String(ValveOutput1Stat) + " Irrigation Valve"
               + "," + getStrTime(startTime)
               + "," + getStrTime(stopTime)
               + "," + String(stopTime - startTime);

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
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening datalog !");
  }
}


// print text on display
void sensorDisplay(String  sensorVal, int xPos, int yPos, int fontSize, int fontColorVal,
                   int delayVal, bool cls, bool clsTxt) {
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
    sensorDisplay("Watering ON", 0, 55, 2, 2, 0, true, false);
  } else {
    sensorDisplay("Watering OFF", 0, 55, 2, 4, 0, true, false);
  }
}


void wateringOff() {
  digitalWrite(Valve_Output1_PIN, LOW);
  analogWrite(pumpPwmPin, pumpPwmDutyCycleOff); // stop the pump
  ValveOutput1Stat = false;
  stopTime = now();
  if (ValveOutput1Stat) {
    sensorDisplay("Watering ON", 0, 55, 2, 2, 0, true, false);
  } else {
    sensorDisplay("Watering OFF", 0, 55, 2, 4, 0, true, false);
  }
  writeDataToSDcard();
  getWaterReservoirState();
}


/*
  Q = Av
  Q = liquid flow rate (m^3/s or L/s)
  A = area of the pipe or channel (m^2)
  v = velocity of the liquid (m/s)
*/
// logic of water flow and Water Tank empty check
float getWaterReservoirState() {

  config.waterReservoirState = config.waterReservoirState - elapseTime * config.flowRate;
  serialOut = true;
  sensorDisplay("elapseTime " + String(elapseTime), 0, 0, 3, 6, 0, true, true);
  sensorDisplay("waterReservoirState " + String(config.waterReservoirState), 0, 20, 3, 6, 0, false, true);
  sensorDisplay("flowRate " + String(config.flowRate), 0, 40, 3, 6, 0, false, true);
  serialOut = false;
  return config.waterReservoirState;
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
  sensorDisplay(headLineStr, 0, 0, 1, 7, 0, false, true);
}


void setupSDcard() {
  sensorDisplay("Initializing SD card...", 0, 0, 1, 7, 0, false, true);
  // see if the card is present and can be initialized:
  if (!SD.begin(SDchipSelect)) {
    sensorDisplay("Card failed, or not present", 0, 10, 1, 7, 0, false, true);
    // don't do anything more:
    // while (1);
    delay (5000);
  }
  else  {
    sensorDisplay("card initialized.", 0, 10, 1, 7, 0, false, true);
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
// {"lastWateringDate":2273,"sensorLastWateringDate":2273,"moistureWateringThreshhold":999,"lightWateringThreshhold":500,"wateringTime":300,"schWateringTime":300,"schWateringFrequency":86400,"schLastWateringDate":2273}
void sdConfig() {
  // Should load default config if run for the first time
  Serial.println(F("Loading configuration..."));
  loadConfiguration(filename, config);

  // Create configuration file
  //  Serial.println(F("Saving configuration..."));
  //  saveConfiguration(filename, config);

  // Dump config file
  Serial.println(F("Print config file..."));
  printFile(filename);
}


// Loads the configuration from a file
void loadConfiguration(const char *filename, Config &config) {
  // Open file for reading
  File file = SD.open(filename);
  if (!file) {
    Serial.println("open config file fail !, create new config file ");
    setConfigValues();
    saveConfiguration(filename, config);
    sensorDisplay("init config", 0, 80, 2, 4, 1000, true, true);
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<512> doc;
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Failed to read file, using default configuration"));
    Serial.println(error.c_str());
    // set default values config
    setConfigValues();
  } else {
    // Copy values from the JsonDocument to the Config
    config.lastWateringDate = doc["lastWateringDate"] | now();
    config.sensorLastWateringDate = doc["sensorLastWateringDate"] | now();
    config.moistureWateringThreshhold = doc["moistureWateringThreshhold"] | 15;
    config.lightWateringThreshhold = doc["lightWateringThreshhold"]  | 100;
    config.wateringTime = doc["wateringTime"] | 300 ; //sec
    config.schWateringTime  = doc["schWateringTime"] | 300;
    config.schWateringFrequency = doc["schWateringFrequency"] | 86400; // 1 day
    config.schLastWateringDate = doc["schLastWateringDate"] | now();

    config.waterReservoirState = doc["waterReservoirState"] | 4 ; //liters
    config.flowRate = doc["flowRate"] | 0.01 ; // liters/sec 1/3600 240 L/H 1/120

  }
  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
}


// Saves the configuration to a file
void saveConfiguration(const char *filename, const Config &config) {
  // Delete existing file, otherwise the configuration is appended to the file
  SD.remove(filename);

  // Open file for writing
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create file "));
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

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  file.close();
}


// Prints the content of a file to the Serial
void printFile(const char *filename) {
  // Open file for reading
  File file = SD.open(filename);
  if (!file) {
    Serial.println(F("Failed to read file"));
    return;
  }
  // Extract each characters by one by one
  while (file.available()) {
    Serial.print((char)file.read());
  }
  Serial.println();
  // Close the file
  file.close();
}


void buttonSetup() {
  //configure pin 3 5 6 as an input and enable the internal pull-up resistor
  pinMode(sensorButton, INPUT_PULLUP);
  pinMode(modeButton, INPUT_PULLUP);
  pinMode(okButton, INPUT_PULLUP);
}


void setConfigValues() {
  // set default values config

  config.moistureWateringThreshhold = 50; //  % dry
  config.lightWateringThreshhold = 100; // % light
  config.wateringTime = 60 ; //sec
  // manual mode
  config.lastWateringDate = now();
  // sensor mode
  config.sensorLastWateringDate = now();
  // sch. mode
  config.schLastWateringDate = now();
  config.schWateringTime = 60;
  config.schWateringFrequency = 86400; // 1 day

  config.waterReservoirState = 4; //liters
  config.flowRate = 0.01; // liters/sec 1/3600
}
