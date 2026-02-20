#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

const char* ssid = "Asus 2,4GHz";
const char* password = "cirozjundrova";

#define USE_LED

// pins
#ifdef USE_LED
const int LED = 2;
#endif

const int BTN = 4;
const int BEEPER = 16;

const int MODE1 = 19;
const int MODE2 = 18;
const int MODE3 = 5;
const int EN    = 21;
const int DIR   = 22;
const int STEP  = 23;


// motor parameters
const int MOTOR_STEPS = 200;
const int MICROSTEP = 128;
const int MICROSTEP_PER_REV = MOTOR_STEPS * MICROSTEP;

int DEG_FOR_FEED_MIN = 60;
int DEG_FOR_FEED_MAX = 360;
int FEED_BEEP = 300;

bool BTN_STATE = 1;
bool PREV_BTN_STATE = 1;
bool FEED = false;

WebServer server(80);

// --------------------------------------------------
// SIMPLE, WORKING MOTOR CODE (from KÓD 2)
// --------------------------------------------------

void rotateDeg(int deg) {

  long steps = (long)MICROSTEP_PER_REV * abs(deg) / 360;

  Serial.printf("Rotating %d deg = %ld steps\n", deg, steps);

  digitalWrite(DIR, deg >= 0 ? HIGH : LOW);

  const int pulse_us = 10;   // speed (10 µs high + 10 µs low = 20 µs period = 50 kHz)

  for (long i = 0; i < steps; i++) {
    digitalWrite(STEP, HIGH);
    delayMicroseconds(pulse_us);
    digitalWrite(STEP, LOW);
    delayMicroseconds(pulse_us);
  }
}

void MotorInit() {
  pinMode(MODE1, OUTPUT);
  pinMode(MODE2, OUTPUT);
  pinMode(MODE3, OUTPUT);
  pinMode(EN, OUTPUT);
  pinMode(DIR, OUTPUT);
  pinMode(STEP, OUTPUT);

  // 1/128 microstep (correct)
  digitalWrite(MODE1, LOW);
  digitalWrite(MODE2, HIGH);
  digitalWrite(MODE3, HIGH);

  digitalWrite(EN, HIGH);
}

// --------------------------------------------------

void zpravaHlavni() {
  String zprava;
  zprava += "<!DOCTYPE html><html>";
  zprava += "<body><h1><a href=\"/feed\">FEED</a></h1><br><br>";
  zprava += "</body></html>";
  server.send(200, "text/html", zprava);
}

void zpravaNeznamy() {
  String zprava = "Neexistujici odkaz\n\n";
  zprava += "URI: ";
  zprava += server.uri();
  server.send(404, "text/plain", zprava);
}

// --------------------------------------------------

void Feed () {
  Serial.println("Feed starting");

  digitalWrite(BEEPER, HIGH);
  delay(FEED_BEEP);
  digitalWrite(BEEPER, LOW);

  int deg = random(DEG_FOR_FEED_MIN, DEG_FOR_FEED_MAX);
  Serial.println("Degrees for feed: " + String(deg) + "°");

#ifdef USE_LED
  digitalWrite(LED, HIGH);
#endif

  rotateDeg(deg);   // ************ motor movement here **************

  delay(100);

#ifdef USE_LED
  digitalWrite(LED, LOW);
#endif

  FEED = false;
  Serial.println("Feed end\n\n");
}

// --------------------------------------------------

void BtnRead(){
  BTN_STATE = digitalRead(BTN);

  if (BTN_STATE == 0 && PREV_BTN_STATE != 0) {
    delay(20);
    if (digitalRead(BTN) == 0) {
      Serial.println("BTN pressed");
      FEED = true;
    }
  }

  PREV_BTN_STATE = BTN_STATE;
}

// --------------------------------------------------

void setup (){

  Serial.begin(115200);
  Serial.println("\nSetup begin\n");

#ifdef USE_LED
  pinMode(LED, OUTPUT);
#endif

  pinMode(BTN, INPUT_PULLUP);
  pinMode(BEEPER, OUTPUT);

  MotorInit();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  ArduinoOTA.setHostname("pet_feeder");
  ArduinoOTA.setPassword("feed");
  ArduinoOTA.begin();

  Serial.println("\nWiFi connected.");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("pet_feeder")) {
      Serial.println("MDNS responder turn on.");
  }

  server.on("/", zpravaHlavni);
  server.on("/feed", []() {
    FEED = true;
    Serial.println("feed turned on");
    zpravaHlavni();
  });
  server.onNotFound(zpravaNeznamy);
  server.begin();

  digitalWrite(BEEPER, HIGH);
  delay(100);
  digitalWrite(BEEPER, LOW);

  Serial.println("Setup end\n\n");
}

// --------------------------------------------------

void loop (){
  ArduinoOTA.handle();
  server.handleClient();
  BtnRead();

  if (FEED)
    Feed();

  delay(1000);

  //rotateDeg(360);  // 1 otočka
  //delay(500);     // pauza 5s
}
