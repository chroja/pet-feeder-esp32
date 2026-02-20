#define USE_LED

// ----------------------------------------
// ENABLE/DISABLE NETWORK (OTA + WebServer)
// ----------------------------------------
// Zapnout síť:
//   #define USE_NET
// Vypnout síť:
//   // #define USE_NET
//#define USE_NET

// ----------------------------------------
#ifdef USE_NET
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

const char* ssid = "Asus 2,4GHz";
const char* password = "cirozjundrova";
WebServer server(80);
#endif



#ifdef USE_LED
const int LED = 2;
#endif

const int BTN = 4;//4;
const int BEEPER = 16;

const int MODE1 = 19;
const int MODE2 = 18;
const int MODE3 = 5;
const int EN    = 21;
const int DIR   = 22;
const int STEP  = 20;//23;

// motor parameters
const int MOTOR_STEPS = 200;
const int MICROSTEP = 128;
const int MICROSTEP_PER_REV = MOTOR_STEPS * MICROSTEP;



// BTN state
bool FEED = false;
bool PREV_BTN = true;

// --------------------------------------------------
// SIMPLE STEPPER ROTATION (blocking microseconds)
// --------------------------------------------------

void rotateStepBurst(long burstSteps) {
  const int pulse_us = 10;

  for (long i = 0; i < burstSteps; i++) {
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
  delay(1000);

  // 1/128 microstep
  digitalWrite(MODE1, LOW);
  digitalWrite(MODE2, HIGH);
  digitalWrite(MODE3, HIGH);
  delay(1000);
  digitalWrite(EN, HIGH);
}

// --------------------------------------------------

#ifdef USE_NET
void zpravaHlavni() {
  String zprava;
  zprava += "<!DOCTYPE html><html><body>";
  zprava += "<h1><a href=\"/feed\">FEED</a></h1>";
  zprava += "</body></html>";
  server.send(200, "text/html", zprava);
}

void zpravaNeznamy() {
  server.send(404, "text/plain", "Neexistujici odkaz");
}
#endif

// --------------------------------------------------
// FEED = jednokrokové pootočení (stará funkce)
// --------------------------------------------------

void Feed() {
  Serial.println("Feed starting");

  digitalWrite(BEEPER, HIGH);
  delay(300);
  digitalWrite(BEEPER, LOW);

  #ifdef USE_LED
    digitalWrite(LED, HIGH);
  #endif

  // původní random feed
  int deg = random(60, 360);
  long steps = (long)MICROSTEP_PER_REV * abs(deg) / 360;

  digitalWrite(DIR, deg >= 0 ? HIGH : LOW);
  rotateStepBurst(steps);

  delay(100);

  #ifdef USE_LED
    digitalWrite(LED, LOW);
  #endif

  FEED = false;
  Serial.println("Feed end");
}

void MoveBack() {
  Serial.println("MoveBack start");

  // původní random feed
  int deg = -5;
  long steps = (long)MICROSTEP_PER_REV * abs(deg) / 360;

  digitalWrite(DIR, deg >= 0 ? HIGH : LOW);
  rotateStepBurst(steps);

  delay(100);

  FEED = false;
  Serial.println("MoveBack end");
}

// --------------------------------------------------
// NEW BEHAVIOR:
//   držím tlačítko → motor se točí
//   pustím → motor se zastaví
//   tlačítko testujeme každých 200 ms
// --------------------------------------------------

void manualRotateWhilePressed() {

  digitalWrite(BEEPER, HIGH);
  delay(150);
  digitalWrite(BEEPER, LOW);

  Serial.println("Manual rotation START");

  digitalWrite(DIR, HIGH);  // fixní směr, klidně změň

  #ifdef USE_LED
    digitalWrite(LED, HIGH);
  #endif

  digitalWrite(EN, HIGH);

  while (digitalRead(BTN) == LOW) {
    rotateStepBurst(300);     // malá dávka kroků
    //delay(200);               // krátká pauza na kontrolu tlačítka
  }
  //digitalWrite(EN, LOW);

#ifdef USE_LED
  digitalWrite(LED, LOW);
#endif

  Serial.println("Manual rotation STOP");
}

// --------------------------------------------------

void setup() {

  Serial.begin(115200);
  Serial.println("Setup begin");

#ifdef USE_LED
  pinMode(LED, OUTPUT);
#endif

  pinMode(BTN, INPUT_PULLUP);
  pinMode(BEEPER, OUTPUT);

  //MotorInit();

#ifdef USE_NET
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
      Serial.println("MDNS responder active.");
  }

  server.on("/", zpravaHlavni);
  server.on("/feed", []() {
    FEED = true;
    Serial.println("feed triggered");
    zpravaHlavni();
  });
  server.onNotFound(zpravaNeznamy);
  server.begin();
#endif

  digitalWrite(BEEPER, HIGH);
  delay(30);
  digitalWrite(BEEPER, LOW);
  /*
  delay(2000);
  MotorInit();
  digitalWrite(BEEPER, HIGH);
  delay(300);
  digitalWrite(BEEPER, LOW);
*/MoveBack();
  Serial.println("Setup done\n");
}

// --------------------------------------------------

void loop() {

#ifdef USE_NET
  ArduinoOTA.handle();
  server.handleClient();
#endif

  bool btnNow = digitalRead(BTN);

  // TLAČÍTKO PRÁVĚ STISKNUTO
  if (btnNow == LOW && PREV_BTN == HIGH) {
    manualRotateWhilePressed();
  }

  PREV_BTN = btnNow;

  if (FEED)
    Feed();

  delay(10);
}
