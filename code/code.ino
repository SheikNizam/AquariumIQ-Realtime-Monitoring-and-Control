#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <RTClib.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ================= WIFI ACCESS POINT =================
const char* wifiSSID = "";
const char* wifiPASS = "";

// ================= DEVICE ID =================
String HARDCODED_ID = "AQM-1234";

// ================= SERVER =================
WebServer server(80);

// ================= VARIABLES =================
String storedUsername = "";
String storedPassword = "";
String selectedSubtype = "";

float minTemp, maxTemp;
float minPH, maxPH;

// ================= RTC =================
RTC_DS3231 rtc;
bool fedMorning = false;
bool fedEvening = false;

// ================= FEEDER =================
Servo feeder;

const int FEEDER_PIN = 4;   // D4
const int FEED_MORNING_HOUR = 8;   // 8 AM
const int FEED_EVENING_HOUR = 16;  // 4 PM

bool feederRunning = false;
bool manualFeedRequest = false;

String ADMIN_PASSWORD = "mInI@2025";

// ================= Temperature =================

const int HEATER_PIN = 27;  // D27
const int COOLER_PIN = 13;  // D13
const int TEMP_SENSOR_PIN = 23; // D23

float presentTemperature = 0.0;
bool tempManualOverride = false; // Manual ON/OFF by admin

// Setup OneWire and DallasTemperature
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature sensors(&oneWire);

// ================= LIGHT CONTROL =================
const int LIGHT_PIN = 2;   // D2

const int LIGHT_ON_HOUR = 18;   // 6 PM
const int LIGHT_DURATION = 8;   // 8 hours

bool lightManualOverride = false;
bool lightState = false;   // true = ON, false = OFF

// ================= pH CONTROL =================

const int PH_PIN = 19;        // Analog pin for pH sensor
const int PUMP_PIN = 32;      // Water circulation pump
const int SOLENOID_PIN = 33;  // Water inlet solenoid

float presentpH = 0.0;
bool phProcessRunning = false;
bool pumpState = false;
bool solenoidState = false;
bool phManualOverride = false;

// ================= ULTRASONIC =================

const int TRIG_PIN = 18;
const int ECHO_PIN = 5;

float waterLevelDistance = 0.0;
float MIN_WATER_LEVEL = 15.0;  // cm (tank low level)
float MAX_WATER_LEVEL = 5.0;   // cm (tank full level)



// ================= LOAD DEFAULTS =================
void loadDefaults(String type) {

  selectedSubtype = type;

  if(type == "Community") {
    minTemp = 22.22; maxTemp = 27.77;
    minPH = 6.5; maxPH = 7.5;
  }
  else if(type == "Cichlid") {
    minTemp = 22.22; maxTemp = 27.77;
    minPH = 7.8; maxPH = 8.5;
  }
  else if(type == "Plants") {
    minTemp = 24.44; maxTemp = 30;
    minPH = 6.0; maxPH = 7.5;
  }
  else if(type == "Brackish") {
    minTemp = 22.22; maxTemp = 27.77;
    minPH = 7.5; maxPH = 8.4;
  }
  else if(type == "Pond") {
    minTemp = 0.55; maxTemp = 30;
    minPH = 6.5; maxPH = 7.5;
  }
}
// ================ Feeder ===========
void runFeeder() {

  feederRunning = true;

  feeder.write(0); 
  delay(1000);
  feeder.write(180);
    
  feederRunning = false;
}


// ================= CHECK FEEDING TIME =================
void checkFeedingTime() {

  DateTime now = rtc.now();

  if (now.hour() == FEED_MORNING_HOUR &&
      now.minute() == 0 &&
      now.second() == 0 &&
      !fedMorning) {

    runFeeder();
    fedMorning = true;
  }

  if (now.hour() == FEED_EVENING_HOUR &&
      now.minute() == 0 &&
      now.second() == 0 &&
      !fedEvening) {

    runFeeder();
    fedEvening = true;
  }

  // reset at midnight
  if (now.hour() == 0 && now.minute() == 1) {
    fedMorning = false;
    fedEvening = false;
  }
}


// ================= CHECK TEMPERATURE =================

void monitorTemperature() {
  sensors.requestTemperatures();
  presentTemperature = sensors.getTempCByIndex(0);

  Serial.print("Temperature: ");
  Serial.print(presentTemperature);
  Serial.print(" °C | Range: ");
  Serial.print(minTemp);
  Serial.print(" - ");
  Serial.println(maxTemp);

  // Automatic control only if not manual override
  if(minTemp > maxTemp){
  Serial.println(" Temperature limits invalid!");
  }

  if (!tempManualOverride) {
    if (presentTemperature > maxTemp) {
      digitalWrite(COOLER_PIN, HIGH);
      digitalWrite(HEATER_PIN, LOW);
      Serial.println("Cooler ON, Heater OFF");
    } else if (presentTemperature < minTemp) {
      digitalWrite(HEATER_PIN, HIGH);
      digitalWrite(COOLER_PIN, LOW);
      Serial.println("Heater ON, Cooler OFF");
    } else {
      digitalWrite(HEATER_PIN, LOW);
      digitalWrite(COOLER_PIN, LOW);
      Serial.println("Temperature in range: All OFF");
    }
  }
}

// ================= LIGHT CONTROL =================
void monitorLight() {

  DateTime now = rtc.now();
  int hour = now.hour();

  if (!lightManualOverride) {

    // Auto ON between 6 PM and 2 AM
    if (hour >= LIGHT_ON_HOUR || hour < (LIGHT_ON_HOUR + LIGHT_DURATION) % 24) {
      digitalWrite(LIGHT_PIN, HIGH);
      lightState = true;
    } else {
      digitalWrite(LIGHT_PIN, LOW);
      lightState = false;
    }
  }
}


// ================= LOGIN PAGE =================
void handleRoot() {

String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">

<style>
@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;600;700&display=swap');
*{margin:0;padding:0;box-sizing:border-box;}
body{font-family:'Poppins',sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}
.card{background:rgba(255,255,255,0.95);padding:40px;border-radius:25px;box-shadow:0 25px 50px rgba(0,0,0,0.2);width:100%;max-width:420px;text-align:center;}
img{width:120px;height:120px;border-radius:50%;margin-bottom:20px;border:4px solid #667eea;padding:10px;background:white;}
h2{color:#2d3748;margin-bottom:30px;}
input{width:100%;padding:15px;margin:15px 0;border-radius:15px;border:2px solid #e2e8f0;font-size:16px;}
button{width:100%;padding:15px;border:none;border-radius:15px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;font-size:16px;font-weight:600;cursor:pointer;}
</style>

</head>
<body>

<div class="card">
<img src="https://shorturl.at/vh8CQ"> 
<h2>AquariumIQ Device Login</h2>

<form action="/verify" method="POST">
<input type="text" name="username" placeholder="Username" required>
<input type="password" name="password" placeholder="Password" required>
<input type="text" name="deviceid" placeholder="Device ID" required>
<button type="submit">Login</button>
</form>

</div>
</body>
</html>
)rawliteral";

server.send(200,"text/html",page);
}

// ================= VERIFY =================
void handleVerify() {

storedUsername = server.arg("username");
storedPassword = server.arg("password");
String enteredID = server.arg("deviceid");

Serial.println("Username: " + storedUsername);
Serial.println("Password: " + storedPassword);
Serial.println("Device ID: " + enteredID);

if(enteredID == HARDCODED_ID){
server.sendHeader("Location","/dashboard");
server.send(303);
}else{
server.send(200,"text/html","<h2>Invalid Device ID </h2>");
}
}

// ================= DASHBOARD =================
void handleDashboard() {

String page = R"rawliteral(
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;600;700&display=swap');
body{font-family:'Poppins',sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px;}
h2{text-align:center;color:white;margin-bottom:30px;}
.card{background:white;padding:30px;margin-bottom:25px;border-radius:25px;text-align:center;}
img{width:100%;max-width:300px;height:200px;object-fit:cover;border-radius:20px;margin-bottom:20px;}
button{width:100%;padding:16px;border:none;border-radius:15px;background:linear-gradient(135deg,#4facfe 0%,#00f2fe 100%);color:white;font-size:18px;font-weight:600;}
</style>
</head>
<body>

<h2>Select Aquarium Type</h2>

<div class="card">
<img src="https://shorturl.at/YvO5k">
<form action="/fresh">
<button>Fresh Water Aquarium</button>
</form>
</div>

<div class="card">
<img src="https://shorturl.at/8MShp"> 
<form action="/salt">
<button>Salt Water Aquarium</button>
</form>
</div>

</body>
</html>
)rawliteral";

server.send(200,"text/html",page);
}

// ================= FRESH =================
void handleFresh(){

String page = R"rawliteral(
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;600;700&display=swap');
body{font-family:'Poppins',sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:30px;text-align:center;}
h2{color:white;margin-bottom:30px;}
button{width:100%;max-width:400px;padding:20px;border-radius:20px;border:none;background:white;font-weight:600;margin:10px auto;display:block;}
</style>
</head>
<body>

<h2>Select Freshwater Type</h2>

<form action="/subtype"><button name="type" value="Community">Freshwater Community Aquarium</button></form>
<form action="/subtype"><button name="type" value="Cichlid">African Cichlid Aquarium</button></form>
<form action="/subtype"><button name="type" value="Plants">Planted & Discus Aquarium</button></form>
<form action="/subtype"><button name="type" value="Brackish">Brackish Water Aquarium</button></form>
<form action="/subtype"><button name="type" value="Pond">Freshwater Pond System</button></form>

</body>
</html>
)rawliteral";

server.send(200,"text/html",page);
}

// ================= SALT =================
void handleSalt(){
server.send(200,"text/html","<h2>Salt Water Aquarium - Coming Soon 🚀</h2>");
}

// ================= SUBTYPE =================
void handleSubtype(){
String type = server.arg("type");
loadDefaults(type);
server.sendHeader("Location","/details");
server.send(303);
}

// ================= DETAILS =================
void handleDetails(){
    DateTime now = rtc.now();
    String currentTime = String(now.hour()) + ":" + String(now.minute());
    String status = feederRunning ? "Running" : "Idle";

    // Check if redirected from save
    String savedMsg = "";
    if(server.hasArg("saved")) {
        savedMsg = "<h3 style='color:green;'>Configuration Saved </h3><br>";
    }

    String page="<html><body style='text-align:center;padding:40px;background:white;'>";
    page += savedMsg; // Show saved message at the top

    page+="<h2>"+selectedSubtype+"</h2>";
    page+="<form action='/save' method='POST'>";
    page+="Min Temp:<input name='minT' value='"+String(minTemp)+"'><br><br>";
    page+="Max Temp:<input name='maxT' value='"+String(maxTemp)+"'><br><br>";
    page+="Min pH:<input name='minPH' value='"+String(minPH)+"'><br><br>";
    page+="Max pH:<input name='maxPH' value='"+String(maxPH)+"'><br><br>";
    page+="<button type='submit'>Save Configuration</button></form><br><br>";

    page+="<h3>Current Time: "+currentTime+"</h3>";
    page+="<h3>Feeder Status: "+status+"</h3><br>";
    String lightStatus = lightState ? "ON" : "OFF";
    page+="<h3>Light Status: "+lightStatus+"</h3><br>";
    page += "<h3>pH Value: " + String(presentpH) + "</h3>";
    page += "<h3>Pump Status: " + String(pumpState ? "ON" : "OFF") + "</h3>";
    page += "<h3>Solenoid Status: " + String(solenoidState ? "ON" : "OFF") + "</h3>";
    page += "<h3>Water Level Distance: " + String(waterLevelDistance) + " cm</h3><br>";

    // Manual feeder controls
    page+="<h3>Manual Feeder Control (Admin)</h3>";
    page+="<form action='/manualfeed' method='POST'>";
    page+="Password:<input type='password' name='adminpass'><br><br>";
    page+="<button name='action' value='ON'>Force Feed</button><br><br>";
    page+="<button name='action' value='OFF'>Stop Feeder</button>";
    page+="</form><br><br>";
    
    // Manual temperature controls
    page += "<h3>Manual Temperature Control (Admin)</h3>";
    page += "<form action='/manualtemp' method='POST'>";
    page += "Password:<input type='password' name='adminpass'><br><br>";
    page += "<button name='action' value='HEATER_ON'>Heater ON</button> ";
    page += "<button name='action' value='HEATER_OFF'>Heater OFF</button><br><br>";
    page += "<button name='action' value='COOLER_ON'>Cooler ON</button> ";
    page += "<button name='action' value='COOLER_OFF'>Cooler OFF</button>";
    page += "</form><br><br>";
    
    // Manual light controll
    page += "<h3>Manual Light Control (Admin)</h3>";
    page += "<form action='/manuallight' method='POST'>";
    page += "Password:<input type='password' name='adminpass'><br><br>";
    page += "<button name='action' value='LIGHT_ON'>Light ON</button> ";
    page += "<button name='action' value='LIGHT_OFF'>Light OFF</button>";
    page += "</form><br><br>";

    page += "<h3>Manual Pump & Solenoid Control (Admin)</h3>";
    page += "<form action='/manualwater' method='POST'>";
    page += "Password:<input type='password' name='adminpass'><br><br>";
    page += "<button name='action' value='PUMP_ON'>Pump ON</button> ";
    page += "<button name='action' value='PUMP_OFF'>Pump OFF</button><br><br>";
    page += "<button name='action' value='SOL_ON'>Solenoid ON</button> ";
    page += "<button name='action' value='SOL_OFF'>Solenoid OFF</button>";
    page += "</form><br><br>";
   
    page+="</body></html>";

    server.send(200,"text/html",page);
}

// ================= SAVE =================
void handleSave(){
  minTemp = server.arg("minT").toFloat();
    maxTemp = server.arg("maxT").toFloat();
    minPH = server.arg("minPH").toFloat();
    maxPH = server.arg("maxPH").toFloat();

    Serial.println("Saved Type: " + selectedSubtype);

    server.sendHeader("Location", "/details?saved=1"); 
    server.send(303); // 303 redirect
}

// ================= MANUAL FEED CONTROL =================
void handleManualFeed(){

    String pass = server.arg("adminpass");
    String action = server.arg("action");

    if(pass == ADMIN_PASSWORD){

        if(action == "ON"){
            runFeeder();
        }
        else if(action == "OFF"){
            feeder.write(92);   // STOP immediately
            feederRunning = false;
        }

        server.sendHeader("Location", "/details");
        server.send(303);
    }
    else{
        server.send(200,"text/html","<h2>Wrong Admin Password</h2>");
    }
}


// ================= MANUAL TEMP CONTROL =================

void handleManualTemp() {
  String pass = server.arg("adminpass");
  String action = server.arg("action");

  if(pass == ADMIN_PASSWORD) {
    if(action == "HEATER_ON") {
      tempManualOverride = true;
      digitalWrite(HEATER_PIN, HIGH);
      digitalWrite(COOLER_PIN, LOW);
    }
    else if(action == "HEATER_OFF") {
      tempManualOverride = false;
      digitalWrite(HEATER_PIN, LOW);
    }
    else if(action == "COOLER_ON") {
      tempManualOverride = true;
      digitalWrite(COOLER_PIN, HIGH);
      digitalWrite(HEATER_PIN, LOW);
    }
    else if(action == "COOLER_OFF") {
      tempManualOverride = false;
      digitalWrite(COOLER_PIN, LOW);
    }

    server.sendHeader("Location", "/details");
    server.send(303);
  } else {
    server.send(200,"text/html","<h2>Wrong Admin Password </h2>");
  }
}

// ================= MANUAL LIGHT CONTROL =================
void handleManualLight() {

  String pass = server.arg("adminpass");
  String action = server.arg("action");

  if(pass == ADMIN_PASSWORD) {

    if(action == "LIGHT_ON") {
      lightManualOverride = true;
      digitalWrite(LIGHT_PIN, HIGH);
      lightState = true;
    }
    else if(action == "LIGHT_OFF") {
      lightManualOverride = true;
      digitalWrite(LIGHT_PIN, LOW);
      lightState = false;
    }

    server.sendHeader("Location", "/details");
    server.send(303);

  } else {
    server.send(200,"text/html","<h2>Wrong Admin Password </h2>");
  }
}

// ================= MANUAL water CONTROL =================
void handleManualWater() {

  String pass = server.arg("adminpass");
  String action = server.arg("action");

  if(pass == ADMIN_PASSWORD) {

    phManualOverride = true;

    if(action == "PUMP_ON") {
      digitalWrite(PUMP_PIN, HIGH);
      pumpState = true;
    }
    else if(action == "PUMP_OFF") {
      digitalWrite(PUMP_PIN, LOW);
      pumpState = false;
    }
    else if(action == "SOL_ON") {
      digitalWrite(SOLENOID_PIN, HIGH);
      solenoidState = true;
    }
    else if(action == "SOL_OFF") {
      digitalWrite(SOLENOID_PIN, LOW);
      solenoidState = false;
    }

    server.sendHeader("Location", "/details");
    server.send(303);

  } else {
    server.send(200,"text/html","<h2>Wrong Admin Password</h2>");
  }
}


// ================= ULTRASONIC =================
float readWaterLevel() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  waterLevelDistance = duration * 0.034 / 2;

  Serial.print("Water Level Distance: ");
  Serial.println(waterLevelDistance);

  return waterLevelDistance;
}

// ====== =========== pH =================
float readpH() {
  int sensorValue = analogRead(PH_PIN);
  float voltage = sensorValue * (3.3  / 4095.0); // ESP32 ADC reference voltage

  // pH sensor calibration values (adjust with your buffers)
  float voltage4 = 1.61;  // Voltage measured at pH 4.0 buffer
  float voltage7 = 2.0;   // Voltage measured at pH 7.0 buffer

  float slope = (7.0 - 4.0) / (voltage7 - voltage4); // slope = ΔpH / ΔV
  float offset = 7.0 - slope * voltage7;             // intercept

  presentpH = -1*(slope * voltage + offset);

  Serial.print("pH: ");
  Serial.println(presentpH);
  return presentpH;
}


// ================= pH =================

void monitorPHAndWater() {

  readpH();
  readWaterLevel();

  if (phManualOverride) return;

  // SAFETY FIRST — WATER LEVEL CHECK

  if (waterLevelDistance > MIN_WATER_LEVEL) {
    // Water too low → Fill
    digitalWrite(SOLENOID_PIN, HIGH);
    digitalWrite(PUMP_PIN, LOW);

    solenoidState = true;
    pumpState = false;

    Serial.println("Water Low → Filling...");
    return;
  }

  if (waterLevelDistance < MAX_WATER_LEVEL) {
    // Water too high → Stop filling
    digitalWrite(SOLENOID_PIN, LOW);
    solenoidState = false;
  }

  // pH LOGIC

  if (presentpH > maxPH && !phProcessRunning) {

    Serial.println("pH High → Start circulation");

    phProcessRunning = true;

    // Step 1: Pump ON, Solenoid OFF
    digitalWrite(PUMP_PIN, HIGH);
    digitalWrite(SOLENOID_PIN, LOW);

    pumpState = true;
    solenoidState = false;

    delay(500);  // 2 minutes

    // Step 2: Pump OFF
    digitalWrite(PUMP_PIN, LOW);
    pumpState = false;

    // Step 3: Fill until max level reached
    digitalWrite(SOLENOID_PIN, HIGH);
    solenoidState = true;

    while (readWaterLevel() > MAX_WATER_LEVEL) {
      delay(500);
    }

    digitalWrite(SOLENOID_PIN, LOW);
    solenoidState = false;

    phProcessRunning = false;
  }

  if (presentpH <= maxPH && presentpH >= minPH) {
    Serial.println("pH Normal");
  }
}


// ================= SETUP =================
void setup(){
Serial.begin(115200);
WiFi.mode(WIFI_STA);
WiFi.begin(wifiSSID, wifiPASS);

Serial.print("Connecting to WiFi");

while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".")   ;
}

Serial.println("\nConnected!");
Serial.print("ESP32 IP Address: ");
Serial.println(WiFi.localIP());


Wire.begin(21, 22);   // SDA = D21, SCL = D22
  
if (!rtc.begin()) {
  Serial.println("RTC not found");
  while (1);
}
if (rtc.lostPower()) {
  rtc.adjust(DateTime(__DATE__, __TIME__));
}
  
feeder.attach(FEEDER_PIN);
feeder.write(92);   // STOP at startup

pinMode(HEATER_PIN, OUTPUT);
pinMode(COOLER_PIN, OUTPUT);
pinMode(TEMP_SENSOR_PIN, INPUT);
pinMode(LIGHT_PIN, OUTPUT);
digitalWrite(LIGHT_PIN, LOW);
digitalWrite(HEATER_PIN, LOW);
digitalWrite(COOLER_PIN, LOW);
pinMode(PUMP_PIN, OUTPUT);
pinMode(SOLENOID_PIN, OUTPUT);
pinMode(TRIG_PIN, OUTPUT);
pinMode(ECHO_PIN, INPUT);
digitalWrite(PUMP_PIN, LOW);
digitalWrite(SOLENOID_PIN, LOW);


sensors.begin();

server.on("/",handleRoot);
server.on("/verify",HTTP_POST,handleVerify);
server.on("/dashboard",handleDashboard);
server.on("/fresh",handleFresh);
server.on("/salt",handleSalt);
server.on("/subtype",handleSubtype);
server.on("/details",handleDetails);
server.on("/save",HTTP_POST,handleSave);
server.on("/manualfeed", HTTP_POST, handleManualFeed);
server.on("/manualtemp", HTTP_POST, handleManualTemp);
server.on("/manuallight", HTTP_POST, handleManualLight); 
server.on("/manualwater", HTTP_POST, handleManualWater);




server.begin();

}

void loop(){

  checkFeedingTime();
  monitorTemperature();
  monitorPHAndWater();
  server.handleClient();
  monitorLight();

}  
