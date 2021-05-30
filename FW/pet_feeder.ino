#include <WiFi.h>
#include <WiFiClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "STSPIN820.h" //https://github.com/laurb9/StepperDriver
 
const char* ssid = "Asus 2,4GHz";
const char* password = "cirozjundrova";

const char* PARAM_INPUT_1 = "output";
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
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 6px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 3px}
    input:checked+.slider {background-color: #b30000}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <h2>ESP Web Server</h2>
  %BUTTONPLACEHOLDER%
<script>function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  console.log(xhr);
    if(element.checked){ xhr.open("GET", "/update?output="+element.id+"&state=1", true); }
  else { xhr.open("GET", "/update?output="+element.id+"&state=0", true); }
  xhr.send();
}
</script>
</body>
</html>
)rawliteral";

// Replaces placeholder with button section in your web page
String processor(const String& var){
  Serial.println(var);
  if(var == "BUTTONPLACEHOLDER"){
    String buttons = "";
    buttons += "<h4>FEED</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"1\" " + outputState(1) + "><span class=\"slider\"></span></label>";
    //buttons += "<h4>Output - GPIO 4</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"4\" " + outputState(4) + "><span class=\"slider\"></span></label>";
    //buttons += "<h4>Output - GPIO 16</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"16\" " + outputState(16) + "><span class=\"slider\"></span></label>";
    return buttons;
  }
  return String();
}

String outputState(int output){
  if(digitalRead(output)){
    return "checked";
  }
  else {
    return "";
  }
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
        // Print local IP address and start web server
        Serial.println("");
        Serial.println("WiFi connected.");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());


        // Route for root / web page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send_P(200, "text/html", index_html, processor);
        });
          // Send a GET request to <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
        server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
            String inputMessage1;
            String inputMessage2;
            // GET input1 value on <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
            if (request->hasParam(PARAM_INPUT_1) && request->hasParam(PARAM_INPUT_2)) {
                inputMessage1 = request->getParam(PARAM_INPUT_1)->value();
                inputMessage2 = request->getParam(PARAM_INPUT_2)->value();
                //digitalWrite(inputMessage1.toInt(), inputMessage2.toInt());
                if (inputMessage1.toInt() == 1){
                    FEED = inputMessage2.toInt();
                }
            }
            else {
            inputMessage1 = "No message sent";
            inputMessage2 = "No message sent";
            }
            Serial.print("GPIO: ");
            Serial.print(inputMessage1);
            Serial.print(" - Set to: ");
            Serial.println(inputMessage2);
            request->send(200, "text/plain", "OK");
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