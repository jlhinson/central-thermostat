// libraries
// =================================================================
#include <Adafruit_SleepyDog.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <WiFi101.h> //change connection timeout from 20000 to 5000 in WiFiClient.cpp
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <PubSubClient.h> //change MQTT_SOCKET_TIMEOUT to 5 in this file
#include <ArduinoJson.h>

// sd intialize
// =================================================================
const int chipSelect = 10;
File myFile;
bool errorSd = false; //track SD error
bool _errorSd = errorSd; //track SD error change

// wifi intialize
// =================================================================
char ssid[] = "jlhnetwork2g"; //  your network SSID (name)
char pass[] = "H61dxU>jmG"; // your network password
int status = WL_IDLE_STATUS; // the Wifi radio's status
boolean wifiNotify = false; // track wifi connection notification
unsigned long lastWifiAttempt = 0; // track connect attempt to delay repeated attempts
bool errorWifi = false; // track wifi error

// display intialize
// =================================================================
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

// pubsubclient intialize
// =================================================================
WiFiClient wifiClient;
PubSubClient client(wifiClient);
unsigned long lastMqttAttempt = 0; // track connect attempt to delay repeated attempts
bool errorMqtt = false; // track mqtt error

// dht initialize
// =================================================================
#define DHTPIN 5
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
unsigned long lastMeasure = 0;
float it = 0; //indoor temp
float ih = 0; //indoor humidity
bool errorDht = false; // track dht error
bool _errorDht = errorDht; // track dht error change

// relay initialize
// =================================================================
int fanRelay = A0;
int compressorRelay = A1;
int heatRelay = A2;
int auxRelay = A3;

// rgb led initialize
// =================================================================
int redPin = 13; //rgb led pins
int greenPin = 12;
unsigned long lastBlink1 = 0; // used for timing light blinking for errors
unsigned long lastBlink2 = 0; //used for timing light blinking for delay

// buttons intialize
// =================================================================
int aButton = 11; //sp
int bButton = 6; //modes
int cButton = A4; //cancel

// other variables for error handling
// =================================================================
bool errorExist = false; //used to indicate an error exist
String errorMsg = ""; //used to store error message
unsigned long compressorStartTime = 0; //used to track when compressor turns on for limiting run time
int errorCompressor1 = 0; //used to track how many times compressor was turned off due to exceeding run time limit
int _errorCompressor1 = errorCompressor1;
int errorCompressor2 = 0; //used to track how many times span was exceeded and possbile compressor issue
int _errorCompressor2 = errorCompressor2;
unsigned long lastSpanCheck = 0; //used to track when SP and "it" span is checked

// other variable initialize default values
// =================================================================
float SP; //setpoint
float auxSP = 25; //setpoint for outdoor temperature to turn on aux heat
float span = 0.9; //used to control how tight the indoor temperature is controled
float auxSpan = 1.5; //used to control when aux heat auto kick in while heating normally
unsigned long coolDown = millis(); //used to prevent compressor damage
bool inDelay = false; //used to indicate if in delay timer
float ot = 0; //outdoor temp
String oh = "0%"; //outdoor humidity, formated this way due to wunderground API response
String systemMode; //Cool, Heat, Aux, Off
String _systemMode = "Off"; //used for comparison
String fanMode; //Auto, On
String _fanMode = "Off"; //used for comparison
bool systemActive = false; //is system currently running cool/heat/aux
int resetPin = A5; //used to reset the arduino manually
String deviceId = "F8F005F1DBE7"; //MAC address

// =================================================================
void setup() {

  Serial.begin(9600); // setup serial

  delay(1000); //initial delay
  /*while (!Serial) { //stops program execution until serial port connected
    ;
  }*/

  if (!SD.begin(chipSelect)) { //start SD
    Serial.println("SD initialization failed");
    errorSd = true;
  }

  if (SD.exists("system.txt")) { //setup system mode
    myFile = SD.open("system.txt");
    char systemModeStr[4] = "";
    int fileIndex = 0;
    while (myFile.available()) {
      systemModeStr[fileIndex] = myFile.read();
      fileIndex++;
    }
    myFile.close();
    systemMode = systemModeStr;
  } else {
    myFile = SD.open("system.txt", FILE_WRITE);
    myFile.print("Off");
    myFile.close();
    systemMode = "Off";
  }

  if (SD.exists("fan.txt")) { //setup fan mode
    myFile = SD.open("fan.txt");
    char fanModeStr[4] = "";
    int fileIndex = 0;
    while (myFile.available()) {
      fanModeStr[fileIndex] = myFile.read();
      fileIndex++;
    }
    myFile.close();
    fanMode = fanModeStr;
  } else {
    myFile = SD.open("fan.txt", FILE_WRITE);
    myFile.print("Auto");
    myFile.close();
    fanMode = "Auto";
  }

  if (SD.exists("sp.txt")) { //setup SP
    myFile = SD.open("sp.txt");
    char SPStr[2];
    int fileIndex = 0;
    while (myFile.available()) {
      SPStr[fileIndex] = myFile.read();
      fileIndex++;
    }
    myFile.close();
    SP = atof(SPStr);
  } else {
    myFile = SD.open("sp.txt", FILE_WRITE);
    myFile.println("74");
    myFile.close();
    SP = 74;
  }

  WiFi.setPins(8,7,4,2); // Define the WINC1500 board connections

  digitalWrite(resetPin, HIGH); //reset pin
  pinMode(resetPin, OUTPUT);

  pinMode(fanRelay, OUTPUT); // relays
  digitalWrite(fanRelay, HIGH);
  pinMode(compressorRelay, OUTPUT);
  digitalWrite(compressorRelay, HIGH);
  pinMode(heatRelay, OUTPUT);
  digitalWrite(heatRelay, HIGH);
  pinMode(auxRelay, OUTPUT);
  digitalWrite(auxRelay, HIGH);
  

  pinMode(redPin, OUTPUT); // rgb led
  pinMode(greenPin, OUTPUT);

  pinMode(aButton, INPUT); // buttons
  pinMode(bButton, INPUT);
  pinMode(cButton, INPUT);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // setup display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.display();

  

  if (WiFi.status() == WL_NO_SHIELD) { // check for the presence of the shield and don't continue if not present
    Serial.println("No wifi shield. Program will not continue.");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("No wifi shield. Program will not continue.");
    display.display();
    while (true);
  }

  client.setServer("192.168.8.5", 1883); // set pubsubclient values
  client.setCallback(callback);

  dht.begin(); // setup dht
  
  delay(2000); // let things settle

  int watchdogMax = Watchdog.enable(); // enable watchdog with max timeout
}

// =================================================================
void loop() {
  
  connections(); // make connections

  unsigned long now = millis();
  
  if (client.connected()) { // call pubsubclient loop
    client.loop(); 
  }

  // fan
  if (fanMode != _fanMode && !systemActive) { //only run if there is a change in fanMode and compressor is not running
    _fanMode = fanMode;
    if (fanMode == "Auto") {
      digitalWrite(fanRelay, HIGH);
    }
    if (fanMode == "On") {
      digitalWrite(fanRelay, LOW);
    }
  }

  // system off; does not control fan, only cool/heat/aux
  if (systemMode != _systemMode && systemMode == "Off") { //only run if systemMode is changed to Off
    _systemMode = systemMode;
    turnOffCompressor();
    if (inDelay) { //ensure delay is removed from display if system is turned off while waiting in delay in another mode
      inDelay = false;
    }
  }
  
  // cool mode
  if (systemMode == "Cool") {
    if (systemMode != _systemMode) { //update _systemMode value
      if (_systemMode != "Off") { //if system was not already off, turn compressor off now before doing anything else
        turnOffCompressor();
      }
      _systemMode = systemMode;
      lastSpanCheck = now;
    }
    if (it != 0 && abs(it-SP) > span && it > SP && !systemActive) { //have to check span as well as comparing to SP since span comparison is absolute; checking systemActive prevents unnecessary running
      turnOnCompressor("Cool");
    }
    if (inDelay && (abs(it-SP) < span || it < SP)) { //ensure delay is removed from display if systems no longer needs to be active while waiting on compressor delay
      inDelay = false;
    }
    if (abs(it-SP) > span && it < SP && systemActive) {
      turnOffCompressor();
    }
  }

  // heat mode
  if (systemMode == "Heat") {
    if (systemMode != _systemMode) { //update _systemMode value
      if (_systemMode == "Cool") { //if switching directly from cool, compressor must be turned off first
        turnOffCompressor();
      }
      if (_systemMode == "Aux") { //if switching from aux, just disable the aux relay
        digitalWrite(auxRelay, HIGH);
      }
      _systemMode = systemMode;
      lastSpanCheck = now;
    }
    if (it != 0 && abs(it-SP) > span && it < SP && !systemActive) { //have to check span as well as comparing to SP since span comparison is absolute; checking systemActive prevents unnecessary running
      turnOnCompressor("Heat");
    }
    if (inDelay && (abs(it-SP) < span || it > SP)) {
      inDelay = false;
    }
    if (abs(it-SP) > span && it > SP && systemActive) {
      turnOffCompressor();
    }
  }

  // aux mode
  if (systemMode == "Aux" || systemMode == "autoAux") {
    if (systemMode != _systemMode) { //update _systemMode value
      if (_systemMode == "Cool") { //if switching directly from cool, compressor must be turned off first
        turnOffCompressor();
      }
      if (_systemMode == "Heat" && systemActive) { //if switching from heat and the compressor is running, just enable the aux relay
        digitalWrite(auxRelay, LOW);
      }
      _systemMode = systemMode;
      lastSpanCheck = now;
    }
    if (it != 0 && abs(it-SP) > span && it < SP && !systemActive) { //have to check span as well as comparing to SP since span comparison is absolute; checking systemActive prevents unnecessary running
      turnOnCompressor("Aux");
    }
    if (inDelay && (abs(it-SP) < span || it > SP)) {
      inDelay = false;
    }
    if (abs(it-SP) > span && it > SP && systemActive) {
      turnOffCompressor();
    }
  }

  if (systemMode == "Heat" || systemMode == "autoAux") { //check to see if aux heating should been enabled/disabled based on ot or span
    checkAux();
  }
  
  measure(); // take measurments and publish

  checkCompressor(); // check how long the compressor has been running
  
  checkErrors(); // check for errors; if mqtt connection error, entire loop will be delayed ~15 seconds due to connection timeout in library

  checkButtons(); // check for button presses to enter manual mode

  Watchdog.reset(); // reset the watchdog
}

// prepare mqtt message with float
// =================================================================
void mqttSendF(String type, String param, float value) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root[param] = value, 1;
  String msg = "";
  root.printTo(msg);
  mqttSend(type, msg);
}

// prepare mqtt message with string
// =================================================================
void mqttSendS(String type, String param, String value) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root[param] = value;
  String msg = "";
  root.printTo(msg);
  mqttSend(type, msg);
}

// send mqtt message
// =================================================================
void mqttSend(String type, String msg) {
  if (client.connected()) {
    char _msg[msg.length()+1];
    msg.toCharArray(_msg, msg.length()+1);
    String topic = "thermostats/" + deviceId + "/" + type;
    char _topic[topic.length()+1];
    topic.toCharArray(_topic, topic.length()+1);
    if (!client.publish(_topic, _msg)) {
      Serial.println("MQTT message failed. Client connected.");
    }
  } else {
    Serial.println("MQTT message failed. Client disconnected.");
    return;
  }
}

// pubsubclient callback
// =================================================================
void callback(char* topic, byte* payload, unsigned int length) {
  // get JSON object to parse
  char message[length];
  for (int i=0;i<length;i++) {
    message[i] = (char)payload[i];
  }
  // parse JSON
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(message);
  // test if parsing succeeds.
  if (!root.success()) {
    Serial.println("Failed to parse JSON recieved in MQTT message");
    return;
  }
  // create string variable for comparison
  String _topic = topic;
  // possible commands directed towards this specific device
  if (_topic == "thermostats/"+deviceId) {
    // create string variable for comparison
    String param = root["param"];
    // fanMode
    if (param == "fanMode") {
      String value = root["value"];
      if (value != "Auto" && value != "On") {
        return;
      }
      fanMode = value;
      mqttSendS("status", "fanMode", value);
      updateTxt("fan", value);
    }
    // systemMode
    if (param == "systemMode") {
      String value = root["value"];
      if (value != "Off" && value != "Cool" && value != "Heat" && value != "Aux") {
        return;
      }
      systemMode = value;
      mqttSendS("status", "systemMode", value);
      updateTxt("system", value);
    }
    // SP
    if (param == "SP") {
      float value = root["value"];
      if (value < 60 || value > 90) {
        return;
      }
      SP = value;
      mqttSendF("status", "SP", value);
      updateTxt("SP", String(value, 0));
    }
     // auxSP
    if (param == "auxSP") {
      float value = root["value"];
      if (value < 1 || value > 40) {
        return;
      }
      auxSP = value;
      mqttSendF("status", "auxSP", value);
    }
    // span
    if (param == "span") {
      float value = root["value"];
      if (value < .4 || value > 2) {
        return;
      }
      span = value;
      mqttSendF("status", "span", value);
    }
    // aux span
    if (param == "auxSpan") {
      float value = root["value"];
      if (value < span || value > 2) {
        return;
      }
      auxSpan = value;
      mqttSendF("status", "auxSpan", value);
    }
    // reset
    if (param == "reset") {
      String value = root["value"];
      if (value == "errorCompressor") { //mqtt send is handled in checkErrors()
        errorCompressor1 = 0;
        errorCompressor2 = 0;
      }
    }
  }
  // handle weather update
  if (_topic == "weather/current") {
    float temp_f = root["temp_f"];
    String relative_humidity = root["relative_humidity"];
    if (temp_f == 0 || relative_humidity.length() > 4) {
      return;
    }
    // set new outdoor values
    ot = temp_f;
    oh = relative_humidity;
  }
  updateDisplay();
}

// wifi and pubsub connect/reconnect
// =================================================================
void connections() {
  // connect to wifi
  if (WiFi.status() != WL_CONNECTED) {
    // connect to network
    errorWifi = true; // wifi not connected
    unsigned long now = millis();
    if (now - lastWifiAttempt > 10000) {
      lastWifiAttempt = now;
      Serial.print("Conencting to SSID: ");
      Serial.println(ssid);
      wifiNotify = false;
      status = WiFi.begin(ssid, pass);
    }
  }
  // notify ip address on new connection
  if (WiFi.status() == WL_CONNECTED && wifiNotify == false){
    // you're connected now
    errorWifi = false; // wifi now connected
    IPAddress ip = WiFi.localIP();
    Serial.print("Wifi connected. IP: ");
    Serial.println(ip);
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Wifi connected. IP:");
    display.println(ip);
    display.display();
    delay(2000);
    wifiNotify = true;
    lastWifiAttempt = 0;
  }
  // connect to mqtt
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    if (!errorMqtt) {
      errorMqtt = true; // mqtt not connected
    }
    unsigned long now = millis();
    if (now - lastMqttAttempt > 15000) { //attempt to connect
      lastMqttAttempt = now;
      Serial.print("MQTT connection failed, rc="); // State why connection failed
      Serial.println(client.state());
      Serial.println("Connecting to MQTT");
      char _deviceId[deviceId.length()+1]; //convert deviceId to char
      deviceId.toCharArray(_deviceId, deviceId.length()+1);
      String sub = "thermostats/" + deviceId; //create char for subscription call
      char _sub[sub.length()+1];
      sub.toCharArray(_sub, sub.length()+1);
      StaticJsonBuffer<200> jsonBuffer; //create json object to send as message
      JsonObject& root = jsonBuffer.createObject();
      root["deviceId"] = deviceId;
      String msg = "";
      root.printTo(msg);
      char _msg[msg.length()+1];
      msg.toCharArray(_msg, msg.length()+1);
      if (client.connect(_deviceId, "disconnected", 0, false, _msg)) { //connect to MQTT broker with will message
        client.subscribe("thermostats"); //subscribe
        client.subscribe(_sub);
        client.subscribe("weather/current");
        Serial.println("Connected to MQTT");
        errorMqtt = false; // mqtt connected
        lastMqttAttempt = 0;
        if (!client.publish("connected", _msg)) {
          Serial.println("MQTT conencted message failed to send");
        }
        mqttSendS("status", "fanMode", fanMode); //send all variables at startup and reconnects in case manually modified or errored while offline
        mqttSendS("status", "systemMode", systemMode);
        mqttSendF("status", "SP", SP);
        mqttSendF("status", "auxSP", auxSP);
        mqttSendF("status", "span", span);
        mqttSendF("status", "auxSpan", auxSpan);
        if (systemActive) {
          mqttSendS("status", "systemActive", "true");
        } else {
          mqttSendS("status", "systemActive", "false");
        }
        mqttSendF("error", "errorCompressor1", errorCompressor1);
        mqttSendF("error", "errorCompressor2", errorCompressor2);
        if (errorDht) {
          mqttSendS("error", "errorDht", "true");
        } else {
          mqttSendS("error", "errorDht", "false");
        }
        if (errorSd) {
          mqttSendS("error", "errorSd", "true");
        } else {
          mqttSendS("error", "errorSd", "false");
        }
      }
    }
  }
}

// take temp and humidity measurements
// =================================================================
void measure() {
  unsigned long now = millis();
  if (now - lastMeasure > 20000) {
    lastMeasure = now;
    // Read temperature as Fahrenheit (isFahrenheit = true)
    it = dht.readTemperature(true);
    // Read humidity
    ih = dht.readHumidity();
    // Check if any reads failed and exit early (to try again).
    if (isnan(it) || isnan(ih)) {
      it = 0;
      ih = 0;
      Serial.println("Error with DHT module");
      errorDht = true;
      return;
    }
    if (errorDht) {
      errorDht = false;
    }
    mqttSendF("status", "Temperature", it); // send sensor data
    mqttSendF("status", "Humidity", ih);
    updateDisplay(); // update display with new indoor measurements
  }
}

// turn on compressor
// =================================================================
void turnOnCompressor(String mode) {
  if (errorCompressor1 > 1 || systemActive) {
    return;
  }
  unsigned long now = millis();
  if (now - coolDown < 300000) { //if cool down period for compressor has not completed, blink the led and do not turn compressor on
    blinkDelay();
    return;
  }
  compressorStartTime = now;
  inDelay = false; //esure delay timer inidication is set to false
  systemActive = true; //tell system compressor is on
  updateDisplay();
  setColor(0, 128);
  mqttSendS("status", "systemActive", "true");
  if (mode == "Cool") {
    digitalWrite(fanRelay, LOW);
    digitalWrite(compressorRelay, LOW);
  }
  if (mode == "Heat") {
    digitalWrite(fanRelay, LOW);
    digitalWrite(compressorRelay, LOW);
    digitalWrite(heatRelay, LOW);
  }
  if (mode == "Aux") {
    digitalWrite(fanRelay, LOW);
    digitalWrite(compressorRelay, LOW);
    digitalWrite(heatRelay, LOW);
    digitalWrite(auxRelay, LOW);
  }
}

// turn off compressor and check fan mode
// =================================================================
void turnOffCompressor() {
  if (!systemActive) { //just return if compressor is already off
    return;
  }
  coolDown = millis();
  compressorStartTime = 0;
  systemActive = false;
  updateDisplay();
  setColor(0, 0);
  mqttSendS("status", "systemActive", "false");
  if (fanMode == "On") { // check fan mode
    digitalWrite(fanRelay, LOW);
    digitalWrite(compressorRelay, HIGH);
    digitalWrite(heatRelay, HIGH);
    digitalWrite(auxRelay, HIGH);
  } else {
    digitalWrite(fanRelay, HIGH);
    digitalWrite(compressorRelay, HIGH);
    digitalWrite(heatRelay, HIGH);
    digitalWrite(auxRelay, HIGH);
  }
}

// check compressor
// =================================================================
void checkCompressor() {
  unsigned long now = millis();
  if (systemActive && compressorStartTime != 0) {
    if (now - compressorStartTime > 1500000) { //1500000 = 25 min
      turnOffCompressor();
      errorCompressor1++;
    }
  }
  if (systemMode != "Off" && errorCompressor1 == 0) {
    float _span = span + 2.5;
    if (it != 0 && abs(it-SP) > _span) {
      if (now - lastSpanCheck > 300000) { //limit checks to every 5 min
        lastSpanCheck = now;
        errorCompressor2++;
      }
    }
  }
}

// check to enable aux heating based on span
// =================================================================
void checkAux() {
  if (systemMode == "Heat" && systemActive) { //check span
    if (it != 0 && it < SP && abs(it-SP) > auxSpan) {
      systemMode = "autoAux";
      updateDisplay();
      mqttSendS("status", "systemMode", "Aux");
    }
  }
  if (systemMode == "Heat" && ot != 0 && ot < auxSP) { //check outdoor temperature
    systemMode = "autoAux";
    updateDisplay();
    mqttSendS("status", "systemMode", "Aux");
  }
  if (systemMode == "autoAux" && !systemActive) { //check to see if mode should be changed back to heat
    float _auxSP = auxSP + 1;
    if (ot > _auxSP) {
      systemMode = "Heat";
      updateDisplay();
      mqttSendS("status", "systemMode", "Heat");
    }
  }
}

// check for any errors
// =================================================================
void checkErrors() {
  if (errorCompressor1 != _errorCompressor1) {
    _errorCompressor1 = errorCompressor1;
    mqttSendF("error", "errorCompressor1", errorCompressor1);
  }
  if (errorCompressor2 != _errorCompressor2) {
    _errorCompressor2 = errorCompressor2;
    mqttSendF("error", "errorCompressor2", errorCompressor2);
  }
  if (errorDht != _errorDht) {
    _errorDht = errorDht;
    if (errorDht) {
      mqttSendS("error", "errorDht", "true");
    }
    if (!errorDht) {
      mqttSendS("error", "errorDht", "false");
    }
  }
  if (errorSd != _errorSd) {
    _errorSd = errorSd;
    if (errorSd) {
      mqttSendS("error", "errorSd", "true");
    }
    if (!errorSd) {
      mqttSendS("error", "errorSd", "false");
    }
  }
  if (errorCompressor1 != 0 || errorCompressor2 != 0 || errorDht || errorWifi || errorMqtt || errorSd) { //handles updating display with errors and blinking led
    errorExist = true;
    unsigned long now = millis();
    if (now - lastBlink1 > 2000) { //mqtt error will blink every ~5 seconds due to connection timeout value in library
      lastBlink1 = now;
      setColor(128, 0);
      delay(500);
      if (systemActive) {
        setColor(0, 128);
      } else {
        setColor(0, 0);
      }
    }
    if (errorCompressor1 != 0 && errorMsg != "Err: C.time") {
      errorMsg = "Err: C.time";
      updateDisplay();
    }
    if (errorCompressor2 != 0 && errorMsg != "Err: C.span" && errorCompressor1 == 0) {
      errorMsg = "Err: C.span";
      updateDisplay();
    }
    if (errorDht && errorMsg != "Err: Sensor" && !errorCompressor1 && !errorCompressor2) {
      errorMsg = "Err: Sensor";
      updateDisplay();
    }
    if (errorWifi && errorMsg != "Err: Wifi" && !errorDht && !errorCompressor1 && !errorCompressor2) {
      errorMsg = "Err: Wifi";
      updateDisplay();
    }
    if (errorMqtt && errorMsg != "Err: MQTT" && !errorDht && !errorWifi && !errorCompressor1 && !errorCompressor2) {
      errorMsg = "Err: MQTT";
      updateDisplay();
    }
    if (errorSd && errorMsg != "Err: SDcard" && !errorMqtt && !errorDht && !errorWifi && !errorCompressor1 && !errorCompressor2) {
      errorMsg = "Err: SDcard";
      updateDisplay();
    }
  } else {
    if (errorExist) {
      errorExist = false;
      errorMsg = "";
      updateDisplay();
    }
  }
}

// check for buttons presses to use manual mode
// =================================================================
void checkButtons() {
  if (digitalRead(aButton) == HIGH) { //SP adjust
    unsigned long aPressed = millis();
    while (digitalRead(cButton) == LOW) { //exit if c is pressed
      display.clearDisplay();
      display.setCursor(0,0);
      display.print("SP: ");
      display.println(SP, 0);
      display.println("A: SP+");
      display.println("B: SP-");
      display.println("C: exit/save");
      display.display();
      delay(100);
      if (digitalRead(aButton) == HIGH && (millis()-aPressed) > 500) { //increment SP and dont trigger increment on initial a button press
        SP = SP+1;
        delay(300); //these delays prevent repeated rapid adjustment
      }
      if (digitalRead(bButton) == HIGH) { //decrement SP
        SP = SP-1;
        delay(300);
      }
      Watchdog.reset(); // reset the watchdog
    }
    updateDisplay();
    mqttSendF("status", "SP", SP);
    updateTxt("SP", String(SP, 0));
    delay(300); //prevent triggering c button press
  }
  if (digitalRead(bButton) == HIGH) { //mode adjust, system and fan
    unsigned long bPressed = millis();
    int aPresses = 0;
    while (digitalRead(cButton) == LOW) { //exit if c is pressed
      display.clearDisplay();
      display.setCursor(0,0);
      display.print("System: ");
      display.println(systemMode);
      display.print("Fan: ");
      display.println(fanMode);
      display.println("A: system, B: fan, C: exit/save");
      display.display();
      delay(100);
      if (digitalRead(aButton) == HIGH) { //adjust system mode if a pressed
        aPresses ++;
        if (aPresses == 5) { //only 4 options available; reset to 1 when 5 reached
          aPresses = 1;
        }
        if (aPresses == 1) {
          systemMode = "Off";
        }
        if (aPresses == 2) {
          systemMode = "Cool";
        }
        if (aPresses == 3) {
          systemMode = "Heat";
        }
        if (aPresses == 4) {
          systemMode = "Aux";
        }
        delay(300);
      }
      if (digitalRead(bButton) == HIGH && (millis()-bPressed) > 500) { //adjust fan mode if b pressed; prevent fan adjust on initial b press
        if (fanMode == "Auto") {
          fanMode = "On";
        } else {
          fanMode = "Auto";
        }
        delay(300);
      }
      Watchdog.reset(); // reset the watchdog
    }
    updateDisplay();
    if (_systemMode != systemMode) { //system mode changed; send updated status and update txt file
      mqttSendS("status", "systemMode", systemMode);
      updateTxt("system", systemMode);
    }
    if (_fanMode != fanMode) {
      mqttSendS("status", "fanMode", fanMode);
      updateTxt("fan", fanMode);
    }
    delay(300); //prevent triggering c button press
  }
  if (digitalRead(cButton) == HIGH) { //clear compressor errors; remove sd card files and reboot
    unsigned long cPressed = millis();
    bool exitLoop = false;
    while (!exitLoop) { //exit if c is pressed
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("A: reset device");
      display.println("B: clear comp. error");
      display.println("C: exit");
      display.display();
      delay(100);
      if (digitalRead(aButton) == HIGH) { //reset device and clear sd card settings
        SD.remove("system.txt");
        SD.remove("fan.txt");
        SD.remove("sp.txt");
        delay(100);
        digitalWrite(resetPin, LOW);
      }
      if (digitalRead(bButton) == HIGH) { //clear compressor errors
        errorCompressor1 = 0;
        errorCompressor2 = 0;
      }
      if (digitalRead(cButton) == HIGH && (millis()-cPressed) > 500) { //exit
        exitLoop = true;
      }
      Watchdog.reset(); // reset the watchdog
    }
  }
  updateDisplay();
  delay(300); //prevent triggering c button press
}

// update txt file when system, fan, or sp change
// =================================================================
void updateTxt(String param, String value) {
  if (param == "system") {
    if (SD.exists("system.txt")) {
      SD.remove("system.txt");
    }
    myFile = SD.open("system.txt", FILE_WRITE);
    if (!myFile) {
      errorSd = true;
      return;
    }
    myFile.print(value);
    myFile.close();
  }
  if (param == "fan") {
    if (SD.exists("fan.txt")) {
      SD.remove("fan.txt");
    }
    myFile = SD.open("fan.txt", FILE_WRITE);
    if (!myFile) {
      errorSd = true;
      return;
    }
    myFile.print(value);
    myFile.close();
  }
  if (param == "SP") {
    if (SD.exists("sp.txt")) {
      SD.remove("sp.txt");
    }
    myFile = SD.open("sp.txt", FILE_WRITE);
    if (!myFile) {
      errorSd = true;
      return;
    }
    myFile.print(value);
    myFile.close();
  }
  if (errorSd) {
    errorSd = false;
  }
}

// blink for compressor cool down delay
// =================================================================
void blinkDelay() {
  if (!inDelay) {
    inDelay = true;
    updateDisplay();
  }
  unsigned long now = millis();
  if (now - lastBlink2 > 2000) {
    lastBlink2 = now;
    setColor(0, 128);
    delay(500);
    setColor(0, 0);
  }
}

// update main oled display
// =================================================================
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.print("SP: ");
  display.print(SP, 0);
  display.print("F   ");
  if (errorExist) {
    display.println(errorMsg);
  } else {
    if (systemActive && systemMode == "Cool") {
      display.println("Cooling...");
    }
    if (systemActive && (systemMode == "Heat" || systemMode == "Aux" || systemMode == "autoAux")) {
      display.println("Heating...");
    }
    if (!systemActive && fanMode == "On") {
      display.println("Fanning...");
    }
    if (!systemActive && fanMode == "Auto") {
      display.println("Relaxing...");
    }
  }
  display.print("Indoor:  ");
  display.print(it, 1);
  display.print("F  ");
  display.print(ih, 0);
  display.println("%");
  display.print("Outdoor: ");
  display.print(ot, 1);
  display.print("F  ");
  display.println(oh);
  display.print("Mode: ");
  if (inDelay) {
    display.print("Delay Fan: ");
  } else {
    if (systemMode != "autoAux") {
      display.print(systemMode);
    } else {
      display.print("Aux");
    }
    display.print("  Fan: ");
  }
  display.println(fanMode);
  display.display();
}

// set rgb led
// =================================================================
void setColor(int red, int green) {
  analogWrite(redPin, red);
  analogWrite(greenPin, green);
}

