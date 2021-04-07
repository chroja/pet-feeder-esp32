#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "DRV8825.h" //https://github.com/laurb9/StepperDriver
 
const char* ssid = "Asus 2,4GHz";
const char* password = "xxx";

//pin
const int BTN = 4;
const int BEEPER = 16;
const int MODE0 = 17;
const int MODE1 = 18;
const int MODE2 = 19;
const int EN = 21;
const int DIR = 22;
const int STEP = 23;

// var
int MOTOR_STEPS = 200;
int MICROSTEP = 16;
int RPM = 50;
int MICROSTEP_PER_REV;

int DEG_FOR_FEED;
int DEG_FOR_FEED_MIN = 60;
int DEG_FOR_FEED_MAX = 360;
int FEED_BEEP = 300;
int DEG_FOR_RETRACTION = 10;

bool BTN_STATE = 1;
bool PREV_BTN_STATE = BTN_STATE;
bool FEED = false;

DRV8825 stepper(MOTOR_STEPS, DIR, STEP, EN, MODE0, MODE1, MODE2);
WebServer server(80);

void zpravaHlavni() {
    String zprava;
    zprava += "<a href=\"/feed\"\">FEED</a><br><br>";
    
    // vytištění zprávy se statusem 200 - OK
    server.send(200, "text/html", zprava);
}
void zpravaNeznamy() {
  // vytvoření zprávy s informací o neexistujícím odkazu
  // včetně metody a zadaného argumentu
  String zprava = "Neexistujici odkaz\n\n";
  zprava += "URI: ";
  zprava += server.uri();
  zprava += "\nMetoda: ";
  zprava += (server.method() == HTTP_GET) ? "GET" : "POST";
  zprava += "\nArgumenty: ";
  zprava += server.args();
  zprava += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    zprava += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  // vytištění zprávy se statusem 404 - Nenalezeno
  server.send(404, "text/plain", zprava);
}

void setup (){
    Serial.begin(115200);
    Serial.println("\nSetup begin\n\n");
    pinMode(BTN,  INPUT_PULLUP);
    pinMode(BEEPER,  OUTPUT);
    pinMode(MODE0,  OUTPUT);
    pinMode(MODE1,  OUTPUT);
    pinMode(MODE2,  OUTPUT);
    pinMode(EN,  OUTPUT);
    pinMode(DIR,  OUTPUT);
    pinMode(STEP,  OUTPUT);
    Serial.println(digitalRead(BTN));

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
    if (MDNS.begin("espwebserver")) {
        Serial.println("MDNS responder turn on.");
    }
    server.on("/", zpravaHlavni);
    server.on("/feed", []() {
                FEED = true;
        Serial.println("feed turned on");
        // vytištění hlavní stránky
        zpravaHlavni();
    });
    server.onNotFound(zpravaNeznamy);
    // zahájení aktivity HTTP serveru
    server.begin();
    Serial.println("HTTP server je zapnuty.");

    SetupStepper(RPM, MICROSTEP);
    Beep(100);
    FEED = false;
    Serial.println("\nSetup end\n");
}





void loop (){

    server.handleClient();
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
    stepper.setEnableActiveState(LOW);
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
    stepper.enable();
    stepper.rotate(DEG_FOR_FEED); 
    delay(300);
    stepper.rotate(-DEG_FOR_RETRACTION);
    stepper.disable();
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