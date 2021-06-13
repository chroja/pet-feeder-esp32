#include <WiFi.h>
#include <WiFiClient.h>

#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "STSPIN820.h" //https://github.com/laurb9/StepperDriver

 
const char* ssid = "Asus 2,4GHz";
const char* password = "cirozjundrova";

const char* PARAM_INPUT_1 = "FEED";
const char* PARAM_INPUT_2 = "state";

//#define USE_LED
#define USE_WIFI


//pin
#ifdef USE_LED
    const int LED = 2;
#endif
const int BTN = 4;
const int BEEPER = 16;

const int MODE0 = 19;
const int MODE1 = 18;
const int MODE2 = 15;

const int EN = 21;
const int DIR = 22;
const int STEP = 23;

// var
int MOTOR_STEPS = 200;
int MICROSTEP = 256;
int RPM = 10;
int MICROSTEP_PER_REV;

int DEG_FOR_FEED;
int DEG_FOR_FEED_MIN = 60;//60
int DEG_FOR_FEED_MAX = 360;//360
int FEED_BEEP = 300;
int DEG_FOR_RETRACTION = 20;


bool BTN_STATE = 1;
bool PREV_BTN_STATE = BTN_STATE;
bool FEED = false;


STSPIN820 stepper(MOTOR_STEPS, DIR, STEP, EN, MODE0, MODE1, MODE2);
#ifdef USE_WIFI
    AsyncWebServer server(80);
#endif

#ifdef USE_WIFI
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h2 {font-size: 3.0rem;}
    p {font-size: 3.0rem;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 34px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}
    input:checked+.slider {background-color: #2196F3}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <h2>ESP Web Server</h2>
  %BUTTONPLACEHOLDER%
<script>function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  console.log(xhr);
  if(element.checked){ xhr.open("GET", "/update?state=1", true); }
  else { xhr.open("GET", "/update?state=0", true); }
  xhr.send();
}

setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var inputChecked;
      var outputStateM;
      if( this.responseText == 1){ 
        inputChecked = true;
        outputStateM = "On";
      }
      else { 
        inputChecked = false;
        outputStateM = "Off";
      }
      document.getElementById("FEED").checked = inputChecked;
      document.getElementById("outputState").innerHTML = outputStateM;
    }
  };
  xhttp.open("GET", "/state", true);
  xhttp.send();
}, 1000 ) ;
</script>
</body>
</html>
)rawliteral";

// Replaces placeholder with button section in your web page
String processor(const String& var){
  //Serial.println(var);
  if(var == "BUTTONPLACEHOLDER"){
    String buttons ="";
    String outputStateValue = outputState();
    buttons+= "<h4>Feed - State <span id=\"outputState\"></span></h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"FEED\" " + outputStateValue + "><span class=\"slider\"></span></label>";
    return buttons;
  }
  return String();
}

String outputState(){
  if(FEED){
    return "checked";
  }
  else {
    return "";
  }
  return "";
}
    
#endif

void setup (){
    Serial.begin(115200);
    Serial.println("\nSetup begin\n\n");
    #ifdef USE_LED
        pinMode(LED, OUTPUT);
    #endif
    pinMode(BTN,  INPUT_PULLUP);
    pinMode(BEEPER,  OUTPUT);
    pinMode(MODE0,  OUTPUT);
    pinMode(MODE1,  OUTPUT);
    pinMode(MODE2,  OUTPUT);
    pinMode(EN,  OUTPUT);
    pinMode(DIR,  OUTPUT);
    pinMode(STEP,  OUTPUT);
    Serial.println(digitalRead(BTN));

    #ifdef USE_WIFI
        Serial.print("Connecting to ");
        Serial.println(ssid);
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
  
      // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    ArduinoOTA.setHostname("pet_feeder");

    // No authentication by default
    // ArduinoOTA.setPassword("admin");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA
        .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_SPIFFS
            type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
        })
        .onEnd([]() {
        Serial.println("\nEnd");
        })
        .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        });

    ArduinoOTA.begin();
  
  
        // Print local IP address and start web server
        Serial.println("");
        Serial.println("WiFi connected.");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());


          // Route for root / web page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send_P(200, "text/html", index_html, processor);
        });

        // Send a GET request to <ESP_IP>/update?state=<inputMessage>
        server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
            String inputMessage;
            String inputParam;
            // GET input1 value on <ESP_IP>/update?state=<inputMessage>
            if (request->hasParam(PARAM_INPUT_1)) {
            inputMessage = request->getParam(PARAM_INPUT_1)->value();
            inputParam = PARAM_INPUT_1;
                
            FEED = !FEED;//inputParam.toInt();
                
            }
            else {
            inputMessage = "No message sent";
            inputParam = "none";
            }
            Serial.println(inputMessage);
            request->send(200, "text/plain", "OK");
        });

        // Send a GET request to <ESP_IP>/state
        server.on("/state", HTTP_GET, [] (AsyncWebServerRequest *request) {
            request->send(200, "text/plain", String(FEED).c_str());
        });

        // Start server
        server.begin();
    #endif

    SetupStepper(RPM, MICROSTEP);
    Serial.println("mode 0: " + String(digitalRead(MODE0)) + "\nmode 1: " + String(digitalRead(MODE1)) + "\nmode 2: " + String(digitalRead(MODE2)));

    Beep(100);
    FEED = false;
    Serial.println("\nSetup end\n");
}





void loop (){

    ArduinoOTA.handle();
    
    #ifdef USE_WIFI
        //server.handleClient();
    #endif


    BtnRead();
    if (FEED) Feed ();

    delay(10);
}

void Beep (int lenght){
    Serial.println("Now beep: " + String(lenght) + " ms");
    digitalWrite(BEEPER, HIGH);
    delay(lenght);
    digitalWrite(BEEPER, LOW);
}

void SetupStepper(int rpm, int microstep){
    //stepper.setEnableActiveState(LOW);
    stepper.enable();
    stepper.begin(rpm);
    stepper.setMicrostep(microstep);
    stepper.rotate(5);
    stepper.disable();
    MICROSTEP_PER_REV = microstep * MOTOR_STEPS;
    Serial.println("Stepper begin with: " + String(rpm) + " RPM");
    Serial.println("Microsteping set to: " + String(microstep));
    Serial.println("Microsteps per rev: " + String(MICROSTEP_PER_REV));
}

void Feed (){
    Serial.println("Feed starting");
    Beep(FEED_BEEP);
    if (DEG_FOR_FEED_MIN > DEG_FOR_FEED_MAX){
        Serial.print("MIN is greater than MAX! MIN and MAX will be swapped. ");
        int a = DEG_FOR_FEED_MIN;
        DEG_FOR_FEED_MIN = DEG_FOR_FEED_MAX;
        DEG_FOR_FEED_MAX = a;
        Serial.print("New MIN is: " + String(DEG_FOR_FEED_MIN) + ". New MAX is: " + String(DEG_FOR_FEED_MAX) + ".");
    }
    DEG_FOR_FEED = random(DEG_FOR_FEED_MIN, DEG_FOR_FEED_MAX);
    Serial.println("Degrees for feed: " + String(DEG_FOR_FEED) + "°");
    #ifdef USE_LED
        digitalWrite(LED, HIGH);
    #endif
    stepper.enable();
    stepper.rotate(DEG_FOR_FEED); 
    delay(300);
    stepper.rotate(-DEG_FOR_RETRACTION);
    stepper.disable();
    #ifdef USE_LED
        digitalWrite(LED, LOW);
    #endif
    FEED = false;
    Serial.println("Feed end\n\n");
}

void BtnRead(){
        BTN_STATE = digitalRead(BTN);
    if ((digitalRead(BTN) == 0) && (PREV_BTN_STATE != 0)){
        delay(20);
        if ((digitalRead(BTN) == 0) && (PREV_BTN_STATE != 0)){
            Serial.println("BTN pressed");
            FEED = true;
        }   
    }
    PREV_BTN_STATE = BTN_STATE;

}

