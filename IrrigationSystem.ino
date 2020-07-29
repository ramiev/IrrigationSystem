#include <ArduinoJson.h>
#include <CurieTime.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <TFT.h>  // Arduino LCD library
#include <SD.h>

/*
  Irrigation system and climate for plants home use
  by Rami Even-Tsur
  eventsur@gmail.com
*/

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
#define onOffButton 3
#define setUpButton 5
#define setDownButton 6

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

unsigned long startTime = 0, stopTime = 0, elapseTime = 0;

// watering Solenoid Valve stat
boolean ValveOutput1Stat = false;

// use for display clear old clock value
char clockPrintout[20] = "";

int showSensorSelect = 0;
int modeSelect = 0;
int okSelect = 0;

struct Config {
  unsigned long lastWateringDate; // Sensor Mode
  int moistureWateringThreshhold; // Sensor mode, moisture value threshhold
  int lightWateringThreshhold; // Sensor mode, light value threshhold
  unsigned long wateringTime; // Sensor mode, watering time and rest watering time in sec.

  unsigned long schWateringTime; // schedule mode, long watering time in sec.
  unsigned long schLastWateringDate; // schedule mode, Last Watering Date in sec.
  unsigned long schWateringFrequency; // schedule mode, time between Watering in sec.

};

const char *filename = "/config.txt";  // <- SD library uses 8.3 filenames
Config config;                         // <- global configuration object

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

  sdConfig();
  buttonSetup();
  wateringOff();
}

void loop() {
  clockDisplay(getStrTime(), 0, 110, 2);
  buttonMenu();
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

  // wait for a moment
  // delay(1000);

  // save current time for clear display and write the new time
  sensorVal.toCharArray(clockPrintout, 20);
}

void buttonMenu() {
  bool cls = false;
  bool clsTxt = false;
  int sensorButtonVal = digitalRead(onOffButton);
  int modeButtonVal = digitalRead(setUpButton);
  int okButtonVal = digitalRead(setDownButton);

  int moistureSensorVal = getMoisture();
  int lightSensorVal = getLightValue();
  float tempC = getTemperatures();

  if ((sensorButtonVal == LOW) ) {
    showSensorSelect += 1;
    cls = true;
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
      if (cls) sensorDisplay("humidity\n" + String(moistureSensorVal), 0, 0, 3, 4, 0, true, true);
      if (moistureSensorVal == getMoisture()) {
        sensorDisplay("humidity\n" + String(moistureSensorVal), 0, 0, 3, 4, 0, false, false);
      } else if (moistureSensorVal != getMoisture()) {
        sensorDisplay("humidity\n" + String(moistureSensorVal), 0, 0, 3, 4, 0, true, false);
      } else {
        sensorDisplay("humidity\n" + String(moistureSensorVal), 0, 0, 3, 4, 0, true, true);
      }
      break;
    case 1:
      if (cls)  sensorDisplay("light\n" + String(lightSensorVal), 0, 0, 3, 5, 0, true, true);
      if (lightSensorVal == getLightValue()) {
        sensorDisplay("light\n" + String(lightSensorVal), 0, 0, 3, 5, 0, false, false);
      } else if (lightSensorVal != getLightValue()) {
        sensorDisplay("light\n" + String(lightSensorVal), 0, 0, 3, 5, 0, true, false);
      } else {
        sensorDisplay("light\n" + String(lightSensorVal), 0, 0, 3, 5, 0, true, true);
      }
      break;
    case 2:
      if (cls) sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, true, true);
      if (tempC == getTemperatures()) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, false, false);
      } else if (tempC != getTemperatures()) {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, true, false);
      } else {
        sensorDisplay("temp\n" + String(tempC), 0, 0, 3, 3, 0, true, true);
      }
      break;
    default:
      showSensorSelect = 0;
      sensorDisplay("", 0, 0, 3, 5, 0, true, true);
      break;
  }

  switch (modeSelect) {
    case 0:
      sensorDisplay("Sensor Mode", 0, 80, 2, 4, 0, cls, clsTxt);
      cls = false;
      sensorMode();
      break;
    case 1:
      sensorDisplay("schedule Mode", 0, 80, 2, 7, 0, cls, clsTxt);
      cls = false;
      scheduleMode();
      break;
    case 2:
      sensorDisplay("Manual Mode", 0, 80, 2, 7, 1000, cls, clsTxt);
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
      ( (now() - config.lastWateringDate) >= config.wateringTime))
  {
    wateringOn();

    do {
      elapseTime = now() - startTime;
      moistureSensorVal = getMoisture();
      if ((moistureSensorVal <= config.moistureWateringThreshhold)) {
        config.wateringTime = elapseTime;
        break;
      }
      sensorDisplay("Moisture Thld " + String(config.moistureWateringThreshhold), 0, 0, 1, 7, 0, false, false);
      sensorDisplay("Light Thld " + String(config.lightWateringThreshhold), 0, 15, 1, 7, 0, false, false);
      sensorDisplay("wateringTime " + String(config.wateringTime), 0, 30, 1, 7, 0, false, false);
      sensorDisplay("ElapseTime " + String(elapseTime) + " sec", 0, 75, 2, 2, 1000, false, true);

    } while (elapseTime <= config.wateringTime);
    wateringOff();
    config.lastWateringDate = now();
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
      if ((moistureSensorVal <= config.moistureWateringThreshhold)) {
        config.wateringTime = elapseTime;
        break;
      }
      sensorDisplay("LastWateringDate " + String(config.schLastWateringDate), 0, 0, 1, 7, 0, false, false);
      sensorDisplay("WateringFrequency " + String(config.schWateringFrequency), 0, 15, 1, 7, 0, false, false);
      sensorDisplay("wateringTime " + String(config.schWateringTime), 0, 30, 1, 7, 0, false, false);
      sensorDisplay("ElapseTime " + String(elapseTime) + " sec", 0, 75, 2, 2, 1000, false, true);
    } while (elapseTime <= config.schWateringTime);
    wateringOff();
    config.schLastWateringDate = now();
    saveConfiguration(filename, config);
  }
}


// watering for preset time long and returns to sensor mode
// wateringTime saved to config file
void manualMode() {
  int moistureSensorVal = getMoisture();

  if ( (ValveOutput1Stat == false) && (okSelect > 1))
  {
    wateringOn();
    do {
      elapseTime = now() - startTime;
      moistureSensorVal = getMoisture();
      if ((moistureSensorVal <= config.moistureWateringThreshhold)) {
        config.wateringTime = elapseTime;
        break;
      }
      sensorDisplay("LastWateringDate " + String(config.lastWateringDate), 0, 0, 1, 7, 0, false, false);
      sensorDisplay("wateringTime " + String(config.wateringTime), 0, 30, 1, 7, 0, false, false);
      sensorDisplay("ElapseTime " + String(elapseTime) + " sec", 0, 75, 2, 2, 1000, false, true);
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
  unsigned long timeZone = 10800; // utc +3 hours in israel
  String incomingByte = "" ; // for incoming serial data
  // Serial.println("set time");

  // send data only when you receive data:
  while (Serial.available() > 0) {
    // read the incoming byte:
    char c = Serial.read();  //gets one byte from serial buffer
    incomingByte += c; //makes the string readString
    if (c == 10) {
      setTime(incomingByte.toInt() + timeZone);
      break;
    } else {
      // Serial.println(c, DEC);
    }
  }
}


int getMoisture() {
  int moistureSensorVal = analogRead(moistureSensorPin);
  return moistureSensorVal;
}

int  getLightValue()  {
  int lightSensorVal = analogRead(lightSensorPin);
  return lightSensorVal;
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


String getStrTime() {
  String timeStr, dateStr;

  timeStr = print2digits(hour()) + ":" + print2digits(minute()) + ":" + print2digits(second());
  dateStr = String(day()) + "/" + String(month()) + "/" + String(year());
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
  dataString = getStrTime()
               + "," + String(getTemperatures()) + "℃"
               + "," + String(getMoisture()) + " moisture"
               + "," + String(getLightValue()) + " light"
               + "," + String(ValveOutput1Stat) + " Irrigation"
               + "," + String(startTime)
               + "," + String(stopTime);

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
  Serial.println(sensorPrintout);

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
  ValveOutput1Stat = false;
  stopTime = now();
  if (ValveOutput1Stat) {
    sensorDisplay("Watering ON", 0, 55, 2, 2, 0, true, false);
  } else {
    sensorDisplay("Watering OFF", 0, 55, 2, 4, 0, true, false);
  }
  writeDataToSDcard();
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

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<512> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error)
    Serial.println(F("Failed to read file, using default configuration"));

  // Copy values from the JsonDocument to the Config
  config.lastWateringDate = doc["lastWateringDate"] | 1595191438;
  config.moistureWateringThreshhold = doc["moistureWateringThreshhold"] | 999;
  config.lightWateringThreshhold = doc["lightWateringThreshhold"]  | 500;
  config.wateringTime = doc["wateringTime"] | 600 ; //sec

  config.schWateringTime  = doc["schWateringTime"] | 600;
  config.schWateringFrequency = doc["schWateringFrequency"] | 600;
  config.schLastWateringDate = doc["schLastWateringDate"] | 1595191438;

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
    Serial.println(F("Failed to create file"));
    return;
  }
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<256> doc;

  // Set the values in the document
  doc["lastWateringDate"] = config.lastWateringDate;
  doc["moistureWateringThreshhold"] = config.moistureWateringThreshhold;
  doc["lightWateringThreshhold"] = config.lightWateringThreshhold;
  doc["wateringTime"] = config.wateringTime;

  doc["schWateringTime"] = config.schWateringTime;
  doc["schWateringFrequency"] = config.schWateringFrequency;
  doc["schLastWateringDate"] = config.schLastWateringDate;


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
  pinMode(onOffButton, INPUT_PULLUP);
  pinMode(setUpButton, INPUT_PULLUP);
  pinMode(setDownButton, INPUT_PULLUP);
}
