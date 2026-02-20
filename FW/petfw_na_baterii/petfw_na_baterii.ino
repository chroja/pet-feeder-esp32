#include <Wire.h>
#include <Adafruit_INA219.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include "esp_task_wdt.h"

/* =================================================
   FEATURE CONFIG
================================================= */

#define USE_MOTOR      1   // motor (pokud je kryt otevřený, motor nejede a svítí červená LED)
#define USE_BEEPER     1   // bzučák pro signalizaci (např. konec nabíjení)
#define USE_LED        1   // LED indikace stavu (stav baterie, rychlosti, hlasitosti, nabíjení, chyb)
#define USE_DISPLAY    1   // OLED displej pro zobrazení napětí, proudu, výkonu, rychlosti, stavu nabíjení a chybových hlášení   
#define USE_INA219     1   // měření napětí a proudu baterie pomocí INA219 (I2C adresa 0x40)
#define USE_INA219_LED 0   // druhý INA219 (0x41) pro měření proudu LEDek
#define USE_INPUT      1   // tlačítko
#define USE_COVER      0   // snímač krytu (INPUT_PULLUP, HIGH=otevřeno) — vypni pokud není zapojen
#define USE_POT        1   // potenciometry pro nastavení rychlosti a hlasitosti (nefungují při otevřeném krytu, protože motor nejede)
#define USE_CHARGER    0   // nabíjení (CN3722 + CHARGE_ENABLE/DETECT piny)
#define USE_INA_CHARGER 0  // INA219 @ 0x44 na vstup CN3722 — měří přesné vstupní napětí a proud; nahrazuje CHARGE_DETECT pin
#define USE_NTC        0   // teplotní ochrana baterie při nabíjení
#define USE_SELFTEST   1   // self-test při startu
#define USE_FLOW       0   // TCRT5000 optický senzor průtoku granulí — detekce prázdného zásobníku
#define USE_OTA        0   // OTA update přes WiFi (ArduinoOTA)

#define USE_I2C (USE_INA219 || USE_DISPLAY || USE_INA219_LED || USE_INA_CHARGER)

#if USE_OTA
#include <WiFi.h>
#include <ArduinoOTA.h>
#endif


/* =================================================
   DEBUG CONFIG
================================================= */

#define DEBUG_LEVEL 3

#define DBG_MOTOR   1
#define DBG_LED     1
#define DBG_BAT     1
#define DBG_INPUT   1
#define DBG_STALL   1
#define DBG_DISPLAY 0

#define SELFTEST_LED_COLORCHECK    1   // 1 = problikne všechny LED stavy při self-testu (ladění jasu/barev)
#define SELFTEST_LED_COLORCHECK_MS 500 // ms trvání každého stavu

#define DBG(level, group, msg) \
  if(DEBUG_LEVEL >= level && group){ Serial.println(msg); }

#define DBG2(level, group, msg, val) \
  if(DEBUG_LEVEL >= level && group){ Serial.print(msg); Serial.println(val); }

#define DBG3(level, group, msg) \
  if(DEBUG_LEVEL >= level && group){ Serial.print(msg); }

/* =================================================
   PINY
================================================= */

#define RPWM 16
#define R_EN 17
#define LPWM 18
#define L_EN 19

#define BTN 13
#define COVER 33

#define POT_SPEED 25
#define POT_BEEP 26

#define BTN_INVERT 1   // RF přijímač: výstup HIGH=příjem (aktivní), LOW=klid → invertujeme
#define BTN2 14        // druhý výstup RF přijímače (zpětný chod po zaseknutí) — uprav pin dle HW
#define BTN2_INVERT 1  // stejná logika jako BTN
#define BEEPER 5
#define NEO_PIN 27

#define CHARGE_ENABLE_PIN 32   // OUTPUT — enable/disable CN3722
#define CHARGE_DETECT_PIN 34   // INPUT ONLY — detekce připojeného zdroje (voltage divider)
#define NTC_PIN           35   // INPUT ONLY ADC — NTC termistor na baterii
#define FLOW_PIN           4   // TCRT5000 D0 výstup (INPUT_PULLUP, FALLING = granule prošla)

/* =================================================
   LED MAP
================================================= */

#define LED_BAT_COUNT    8   // počet LED pro baterii (snadno změn na 4)
#define LED_SPEED_COUNT  8   // počet LED pro potenciometr rychlosti (snadno změn na 8)
#define LED_BEEP_COUNT   8   // počet LED pro potenciometr hlasitosti (snadno změn na 8)
#define LED_STATUS_COUNT 16

#define LED_BAT_START 0
#define LED_SPEED_START (LED_BAT_START + LED_BAT_COUNT)
#define LED_BEEP_START  (LED_SPEED_START + LED_SPEED_COUNT)
#define LED_STATUS_START (LED_BEEP_START + LED_BEEP_COUNT)

#define NUM_PIXELS (LED_BAT_COUNT + LED_SPEED_COUNT + LED_BEEP_COUNT + LED_STATUS_COUNT)

/* =================================================
   CONFIG
================================================= */

#define MOTOR_INVERT_DIR 0   // 1 = invertovat směr motoru (pokud je motor zapojen obráceně)

#define STALL_CURRENT 0.15
#define REVERSE_TIME 500
#define MAX_STALL_RETRIES 2

#define BAT_FULL 16.8
#define BAT_EMPTY 11.3

#define BEEP_DURATION 200
#define BEEP_FREQ     2000   // Hz — rezonanční frekvence pasivního bzučáku (KY-006=2kHz, jiné zkus 1-4kHz)

#define INA219_LED_ADDR        0x41
#define INA_CHARGER_ADDR       0x44   // INA219 A1=VS, A0=GND — na vstup CN3722
#define CHARGER_MIN_V          14.0   // V — minimální vstupní napětí pro zahájení nabíjení
#define CHARGER_MAX_V          24.0   // V — maximální povolené vstupní napětí (ochrana)
#define CHARGE_FULL_CURRENT    0.064   // A — konec nabíjení (0.02C pro 3200mAh)
#define CHARGE_TEMP_MAX        45.0    // °C — max teplota při nabíjení
#define NTC_BETA               3950    // beta koeficient NTC termistoru
#define NTC_NOMINAL            10000   // jmenovitý odpor NTC při 25°C (10 kΩ)
#define NTC_SERIES             10000   // sériový rezistor (10 kΩ)
#define NTC_TEMP_NOMINAL       25.0    // referenční teplota NTC (°C)
#define LED_TEST_MIN_CURRENT   0.180   // A — minimální delta proud pro průchozí LED test (18 LED × 10 mA)
#define MOTOR_TEST_DURATION    50      // ms — délka testu motoru
#define MOTOR_TEST_MIN_CURRENT 0.005   // A — minimální delta proud pro detekci motoru (měříme přírůstek oproti baseline)

#define POT_MIN  150   // ADC hodnota na minimu otočení (0V+offset) — kalibruj dle HW
#define POT_MAX  3945  // ADC hodnota na maximu otočení (3.3V-offset) — kalibruj dle HW

#define FLOW_NO_PULSE_TIMEOUT 3000   // ms bez impulzu při chodu motoru → prázdný zásobník

#define OTA_WIFI_SSID  "your_ssid"      // WiFi SSID pro OTA update — změň před kompilací
#define OTA_WIFI_PASS  "your_password"  // WiFi heslo pro OTA update — změň před kompilací
#define OTA_HOSTNAME   "petfeeder"      // mDNS hostname (petfeeder.local v Arduino IDE)

#define WDT_TIMEOUT_S  10   // watchdog timeout (s) — reset ESP32 pokud loop zamrzne

// ----- BARVY LED (R, G, B, hodnoty 0-255) -----
// Baterie — statické stavy
#define COL_BAT_LOW     150,   0,   0   // červená  — málo (<35 %)
#define COL_BAT_MID     150, 90,   0   // žlutá    — střed (35–70 %)
#define COL_BAT_HIGH      0, 150,   0   // zelená   — dost (70–90 %)
#define COL_BAT_FULL      0, 150,   0   // zelená   — plná (>90 %)
#define COL_BAT_NTC     150,  40,   0   // oranžová — přehřátí (USE_NTC)
#define COL_BAT_CHG_FULL  0, 130,   0   // zelená   — nabíjení dokončeno (USE_CHARGER)
// Baterie — dýchání (min/max jas kanálu 0–255)
#define LED_BAT_BREATH_MIN   30         // min jas při vybité baterii (dýchání)
#define LED_BAT_BREATH_MAX  255         // max jas při vybité baterii (dýchání)
#define LED_CHG_BREATH_MIN   30         // min jas při nabíjení (dýchání)
#define LED_CHG_BREATH_MAX  230         // max jas při nabíjení (dýchání)
// Potenciometry
#define COL_POT_SPEED     0,  40, 100   // barva LED sekce rychlosti
#define COL_POT_BEEP      0,  40, 100   // barva LED sekce hlasitosti
// Status LEDky
#define COL_STATUS_OK     0, 100,   0   // zelená   — vše OK
#define COL_STATUS_ERR  100,   0,   0   // červená  — chyba
#define COL_STATUS_CHG    0,   0, 80   // modrá    — nabíjení aktivní
#define COL_STATUS_FULL   0, 100,   0   // zelená   — nabito
#define COL_STATUS_TEMP 100,  30,   0   // oranžová — teplotní chyba nabíjení

/* =================================================
   OBJEKTY
================================================= */

#if USE_INA219
Adafruit_INA219 ina219;
bool inaOK=false;
#endif

#if USE_INA219_LED
Adafruit_INA219 ina219led(INA219_LED_ADDR);
bool inaLedOK=false;
#endif

#if USE_INA_CHARGER
Adafruit_INA219 ina_chg(INA_CHARGER_ADDR);
bool inaChgOK=false;
#endif

#if USE_DISPLAY
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#endif

#if USE_LED
Adafruit_NeoPixel pixels(NUM_PIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);
#endif

/* =================================================
   STAV
================================================= */

float voltage=0;
float current=0;
float power=0;

#if USE_INA219_LED
float ledCurrent=0;
#endif

#if USE_INA_CHARGER
float chargerVoltage=0;        // vstupní napětí CN3722 (17–20 V při připojeném zdroji)
float chargerInputCurrent=0;   // vstupní proud nabíječe
#endif

#if USE_NTC
float ntcTemp=0;
#endif

bool errorState=false;
char errorMsg[32] = "OK";

#if USE_FLOW
volatile uint32_t flowPulseCount = 0;
volatile unsigned long flowLastPulseT = 0;
bool hopperEmpty = false;
void IRAM_ATTR flowISR(){
  flowPulseCount++;
  flowLastPulseT = millis();
}
#endif

#if USE_CHARGER
enum ChargeState{
  CHARGE_DISCONNECTED,
  CHARGE_ACTIVE,
  CHARGE_FULL,
  CHARGE_TEMP_ERROR
};
ChargeState chargeState=CHARGE_DISCONNECTED;
bool chargeFull_signaled=false;
#endif

#if USE_MOTOR
unsigned long stallTimer=0;
unsigned long reverseStart=0;
int stallRetryCount=0;

enum MotorState{
  MOTOR_IDLE,
  MOTOR_FORWARD,
  MOTOR_REVERSE_RETRY,
  MOTOR_ERROR,
  MOTOR_MANUAL_REVERSE   // ruční zpětný chod přes BTN2 po zaseknutí
};

MotorState motorState=MOTOR_IDLE;
#endif

#if USE_BEEPER
const int pwmChanBeep=2;
unsigned long beepStart=0;
bool beepActive=false;
bool prevBtnPressed=false;
#endif

/* =================================================
   MOTOR
================================================= */
#if USE_MOTOR

int pwmChanR = 0;
int pwmChanL = 1;

void motorStop()
{
  DBG(1,DBG_MOTOR,"Motor STOP");
  ledcWrite(pwmChanR,0);
  ledcWrite(pwmChanL,0);
  digitalWrite(R_EN,LOW);
  digitalWrite(L_EN,LOW);
}

void motorRun(bool forward,int percent)
{
  int pwm=map(percent,0,100,0,255);

#if MOTOR_INVERT_DIR
  forward=!forward;
#endif

  DBG2(2,DBG_MOTOR, forward?"Motor FWD ":"Motor REV ", percent);
  digitalWrite(R_EN,HIGH);
  digitalWrite(L_EN,HIGH);

  if(forward){
    ledcWrite(pwmChanR,pwm);
    ledcWrite(pwmChanL,0);
  }else{
    ledcWrite(pwmChanL,pwm);
    ledcWrite(pwmChanR,0);
  }
}

void handleMotorState(int speedPercent,bool btnPressed,bool btn2Pressed)
{
  switch(motorState){

    case MOTOR_IDLE:
      if(btnPressed && !errorState){
        stallRetryCount=0;
        stallTimer=0;
        Serial.println("\n>>> BTN PRESSED — START FEEDING");
        motorRun(true,speedPercent);
        motorState=MOTOR_FORWARD;
      }
      break;

    case MOTOR_FORWARD:
      if(!btnPressed){
        motorStop();
        motorState=MOTOR_IDLE;
        Serial.println("BTN released — STOP FEEDING\n");
        break;
      }

      // Průběžná aktualizace rychlosti při otáčení (reflektuje pohyb POT_SPEED)
      motorRun(true,speedPercent);

      // Průběžný výpis stavu krmení
      {
        static unsigned long feedPrintT=0;
        if(millis()-feedPrintT>=300){
          feedPrintT=millis();
          Serial.print("  feeding... spd=");
          Serial.print(speedPercent);
          Serial.print("%");
#if USE_INA219
          if(inaOK){
            Serial.print("  I="); Serial.print(current,3); Serial.print("A");
          }
#endif
          Serial.println();
        }
      }

      if(current>STALL_CURRENT && speedPercent>20){

        if(stallTimer==0){
          stallTimer=millis();
          Serial.print("  ! STALL detected (I=");
          Serial.print(current,3);
          Serial.println("A) ...");
        }

        if(millis()-stallTimer>300){
          motorStop();
          stallRetryCount++;
          Serial.print("  ! STALL confirmed -> reverse retry ");
          Serial.print(stallRetryCount); Serial.print("/"); Serial.println(MAX_STALL_RETRIES);
          motorRun(false,40);
          reverseStart=millis();
          motorState=MOTOR_REVERSE_RETRY;
        }

      }else stallTimer=0;

      break;

    case MOTOR_REVERSE_RETRY:
      {
        static bool revPrinted=false;
        if(!revPrinted){ Serial.println("  reversing..."); revPrinted=true; }

        if(millis()-reverseStart>REVERSE_TIME){
          revPrinted=false;
          motorStop();

          if(stallRetryCount>=MAX_STALL_RETRIES){
            errorState=true;
            strcpy(errorMsg,"Feeding error");
            motorState=MOTOR_ERROR;
            Serial.println("!!! MOTOR ERROR: max retries exceeded");
            Serial.println("    -> BTN2 (RF ch2) = ruční zpětný chod, uvolnění = reset chyby");
          }else{
            Serial.println("  resuming forward...");
            motorRun(true,speedPercent);
            motorState=MOTOR_FORWARD;
          }
        }
      }
      break;

    case MOTOR_ERROR:
      motorStop();
      // BTN2 = ruční zpětný chod, resetuje chybu
      if(btn2Pressed){
        errorState=false;
        strcpy(errorMsg,"OK");
        stallRetryCount=0;
        stallTimer=0;
        motorRun(false,40);
        motorState=MOTOR_MANUAL_REVERSE;
        Serial.println("BTN2: ruční zpětný chod — chyba resetována");
      }
      break;

    case MOTOR_MANUAL_REVERSE:
      if(!btn2Pressed){
        motorStop();
        motorState=MOTOR_IDLE;
        Serial.println("BTN2 uvolněno — zpětný chod dokončen, IDLE");
      } else {
        static unsigned long manRevPrintT=0;
        if(millis()-manRevPrintT>=500){
          manRevPrintT=millis();
          Serial.print("  manual reverse...");
#if USE_INA219
          if(inaOK){ Serial.print("  I="); Serial.print(current,3); Serial.print("A"); }
#endif
          Serial.println();
        }
      }
      break;
  }
}
#endif

/* =================================================
   LED
================================================= */
#if USE_LED

void drawBarLED(int start,int count,int level,uint32_t color)
{
  if(level > count) level = count;

  for(int i=0;i<count;i++){
    int idx=start+i;
    if(idx>=NUM_PIXELS) return;

    if(i<level) pixels.setPixelColor(idx,color);
    else pixels.setPixelColor(idx,0);
  }
}

void updateBatteryLED(float v)
{
  // Přehřátí (USE_NTC): bliká 1 Hz, 50% duty — nejvyšší priorita
#if USE_NTC
  if(ntcTemp > CHARGE_TEMP_MAX){
    bool on=((millis()/500)%2==0);
    uint32_t c=on?pixels.Color(COL_BAT_NTC):0;
    for(int i=0;i<LED_BAT_COUNT;i++) pixels.setPixelColor(LED_BAT_START+i,c);
    return;
  }
#endif

  // Nabíjení: dýchá zeleně (ACTIVE) / svítí zeleně (FULL)
#if USE_CHARGER
  if(chargeState==CHARGE_ACTIVE){
    float breath=(sinf((float)millis()*0.00314f)+1.0f)*0.5f;  // 0.0-1.0, ~2s perioda
    uint8_t b=(uint8_t)(LED_CHG_BREATH_MIN + breath*(LED_CHG_BREATH_MAX-LED_CHG_BREATH_MIN));
    for(int i=0;i<LED_BAT_COUNT;i++) pixels.setPixelColor(LED_BAT_START+i,pixels.Color(0,b,0));
    return;
  }
  if(chargeState==CHARGE_FULL){
    for(int i=0;i<LED_BAT_COUNT;i++) pixels.setPixelColor(LED_BAT_START+i,pixels.Color(COL_BAT_CHG_FULL));
    return;
  }
#endif

  // Normální zobrazení stavu baterie
  float pct=(v-BAT_EMPTY)/(BAT_FULL-BAT_EMPTY)*100.0f;
  pct=constrain(pct,0.0f,100.0f);

  if(pct<10.0f){
    // 0–9 %: celá baterie dýchá červeně
    float breath=(sinf((float)millis()*0.00314f)+1.0f)*0.5f;
    uint8_t b=(uint8_t)(LED_BAT_BREATH_MIN + breath*(LED_BAT_BREATH_MAX-LED_BAT_BREATH_MIN));
    for(int i=0;i<LED_BAT_COUNT;i++) pixels.setPixelColor(LED_BAT_START+i,pixels.Color(b,0,0));
    return;
  }

  // 10–100 %: procentuální hranice → dynamický počet LEDek
  // Škáluje se automaticky: pro 8 LED = 2/4/6/8, pro 4 LED = 1/2/3/4
  int litCount;
  uint32_t color;
  if(pct<35.0f){
    litCount=max(1, LED_BAT_COUNT/4);        // ~25%
    color=pixels.Color(COL_BAT_LOW);
  } else if(pct<70.0f){
    litCount=max(1, LED_BAT_COUNT/2);        // ~50%
    color=pixels.Color(COL_BAT_MID);
  } else if(pct<90.0f){
    litCount=max(1, LED_BAT_COUNT*3/4);      // ~75%
    color=pixels.Color(COL_BAT_HIGH);
  } else {
    litCount=LED_BAT_COUNT;                  // 100%
    color=pixels.Color(COL_BAT_FULL);
  }

  drawBarLED(LED_BAT_START,LED_BAT_COUNT,litCount,color);
}

void updatePotLED(int percent,int start,int count,uint32_t color)
{
  // Počet rozsvícených LEDek odpovídá nastavené hodnotě (0%=0 LED, 100%=count LEDs)
  int level=constrain((int)roundf(percent*count/100.0f),0,count);
  drawBarLED(start,count,level,color);
}

void updateStatusLED()
{
#if USE_CHARGER
  static unsigned long chargeAnimT=0;
  static int chargeAnimLevel=0;

  if(chargeState==CHARGE_ACTIVE){
    if(millis()-chargeAnimT>800){
      chargeAnimT=millis();
      chargeAnimLevel=(chargeAnimLevel+1)%(LED_STATUS_COUNT+1);
    }
    drawBarLED(LED_STATUS_START,LED_STATUS_COUNT,chargeAnimLevel,pixels.Color(COL_STATUS_CHG));
    return;
  }

  if(chargeState==CHARGE_FULL){
    drawBarLED(LED_STATUS_START,LED_STATUS_COUNT,LED_STATUS_COUNT,pixels.Color(COL_STATUS_FULL));
    return;
  }

  if(chargeState==CHARGE_TEMP_ERROR){
    static unsigned long blinkT=0;
    static bool blinkOn=false;
    if(millis()-blinkT>300){ blinkT=millis(); blinkOn=!blinkOn; }
    drawBarLED(LED_STATUS_START,LED_STATUS_COUNT,
               blinkOn?LED_STATUS_COUNT:0,pixels.Color(COL_STATUS_TEMP));
    return;
  }
#endif

  uint32_t color=errorState ?
    pixels.Color(COL_STATUS_ERR) :
    pixels.Color(COL_STATUS_OK);

  drawBarLED(LED_STATUS_START,LED_STATUS_COUNT,
             LED_STATUS_COUNT,color);
}
#endif

/* =================================================
   DISPLAY
================================================= */
#if USE_DISPLAY
void updateDisplay(int speed, int beep)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  // ── Řádek 1: napětí · proud · výkon ───────────────────────────
  u8g2.setCursor(0,  10); u8g2.print("U:"); u8g2.print(voltage, 1); u8g2.print("V");
  u8g2.setCursor(44, 10); u8g2.print("I:"); u8g2.print(current, 2); u8g2.print("A");
  u8g2.setCursor(90, 10); u8g2.print("P:"); u8g2.print(power,   1); u8g2.print("W");

  // ── Řádek 2: rychlost · hlasitost · % baterie ─────────────────
  float batPct = constrain(
    (voltage - BAT_EMPTY) / (BAT_FULL - BAT_EMPTY) * 100.0f,
    0.0f, 100.0f);
  u8g2.setCursor(0,  22); u8g2.print("Spd:"); u8g2.print(speed);
  u8g2.setCursor(42, 22); u8g2.print("Bp:"); u8g2.print(beep);
  u8g2.setCursor(80, 22); u8g2.print("Bat:"); u8g2.print((int)batPct); u8g2.print("%");

  // ── Oddělovač ──────────────────────────────────────────────────
  u8g2.drawHLine(0, 25, 128);

  // ── Velký stav (dolní polovina 26–64) ─────────────────────────
  //    Priorita: motor akce > chyba > nabíjení > OK
  const char* statusText = "OK";
  bool useBigFont = true;

#if USE_MOTOR
  if     (motorState == MOTOR_FORWARD)             statusText = "FEED";
  else if(motorState == MOTOR_REVERSE_RETRY)       statusText = "UNJAM";
  else if(motorState == MOTOR_MANUAL_REVERSE)      statusText = "REV";
  else
#endif
  if(errorState){
    statusText = errorMsg;
    useBigFont = false;
  }
#if USE_CHARGER
  else if(chargeState == CHARGE_ACTIVE)            statusText = "CHRG";
  else if(chargeState == CHARGE_FULL)              statusText = "FULL";
  else if(chargeState == CHARGE_TEMP_ERROR)      { statusText = errorMsg; useBigFont = false; }
#endif

  if(useBigFont)
    u8g2.setFont(u8g2_font_10x20_tf);
  else
    u8g2.setFont(u8g2_font_7x13B_tf);

  int textW = (int)u8g2.getStrWidth(statusText);
  u8g2.setCursor(max(0, (128 - textW) / 2), useBigFont ? 53 : 50);
  u8g2.print(statusText);

  u8g2.sendBuffer();
}
#endif

/* =================================================
   INPUT
================================================= */
#if USE_INPUT
bool readButton()
{
  bool pressed=(digitalRead(BTN)==LOW);
  if(BTN_INVERT) pressed=!pressed;
  return pressed;
}
#endif

/* =================================================
   INPUT — BTN2
================================================= */
#if USE_MOTOR
bool readButton2()
{
  bool pressed=(digitalRead(BTN2)==LOW);
  if(BTN2_INVERT) pressed=!pressed;
  return pressed;
}
#endif

/* =================================================
   NTC
================================================= */
#if USE_NTC
float readNTCtemp()
{
  int adc=analogRead(NTC_PIN);
  if(adc<=0) return -99.0;
  float r=NTC_SERIES*((4095.0/adc)-1.0);
  float t=log(r/NTC_NOMINAL)/(float)NTC_BETA + 1.0/(NTC_TEMP_NOMINAL+273.15);
  return 1.0/t - 273.15;
}
#endif

/* =================================================
   SELFTEST
================================================= */
#if USE_SELFTEST

bool i2cProbe(uint8_t addr)
{
  Wire.beginTransmission(addr);
  return Wire.endTransmission()==0;
}

#define ST_R(v) ((v)<0?"--":((v)?"OK":"ER"))

#if USE_DISPLAY
void showSelfTestDisplay(
  int inaOk, int inaLedOk,
  int batOk, int motorOk, int ledOk,
  int btnOk, int coverOk, int potOk)
{
  u8g2.clearBuffer();
  u8g2.setCursor(0,10);  u8g2.print("-- SELF TEST --");
  u8g2.setCursor(0,20);  u8g2.print("INA:"); u8g2.print(ST_R(inaOk));
  u8g2.setCursor(56,20); u8g2.print("LED:"); u8g2.print(ST_R(inaLedOk));
  u8g2.setCursor(0,30);  u8g2.print("Bat:"); u8g2.print(ST_R(batOk));
  u8g2.setCursor(56,30); u8g2.print("Mot:"); u8g2.print(ST_R(motorOk));
  u8g2.setCursor(0,40);  u8g2.print("BTN:"); u8g2.print(ST_R(btnOk));
  u8g2.setCursor(56,40); u8g2.print("CVR:"); u8g2.print(ST_R(coverOk));
  u8g2.setCursor(0,50);  u8g2.print("POT:"); u8g2.print(ST_R(potOk));
  u8g2.setCursor(56,50); u8g2.print("LED:"); u8g2.print(ST_R(ledOk));
  u8g2.sendBuffer();
}
#endif

#if USE_LED && SELFTEST_LED_COLORCHECK
void ledColorCheck()
{
  Serial.println("LED color check (jas + barvy)...");

  // pomocný makro: clear → zaplnit sekci → show → čekat
  #define LC(start,count,col,level) \
    pixels.clear(); \
    drawBarLED((start),(count),(level),pixels.Color(col)); \
    pixels.show(); delay(SELFTEST_LED_COLORCHECK_MS);

  // Baterie — 4 stavové úrovně
  LC(LED_BAT_START, LED_BAT_COUNT, COL_BAT_LOW,  max(1, LED_BAT_COUNT/4))
  LC(LED_BAT_START, LED_BAT_COUNT, COL_BAT_MID,  max(1, LED_BAT_COUNT/2))
  LC(LED_BAT_START, LED_BAT_COUNT, COL_BAT_HIGH, max(1, LED_BAT_COUNT*3/4))
  LC(LED_BAT_START, LED_BAT_COUNT, COL_BAT_FULL, LED_BAT_COUNT)

  // Potenciometry
  LC(LED_SPEED_START, LED_SPEED_COUNT, COL_POT_SPEED, LED_SPEED_COUNT)
  LC(LED_BEEP_START,  LED_BEEP_COUNT,  COL_POT_BEEP,  LED_BEEP_COUNT)

  // Status sekce
  LC(LED_STATUS_START, LED_STATUS_COUNT, COL_STATUS_OK,   LED_STATUS_COUNT)
  LC(LED_STATUS_START, LED_STATUS_COUNT, COL_STATUS_ERR,  LED_STATUS_COUNT)
  LC(LED_STATUS_START, LED_STATUS_COUNT, COL_STATUS_CHG,  LED_STATUS_COUNT)
  LC(LED_STATUS_START, LED_STATUS_COUNT, COL_STATUS_FULL, LED_STATUS_COUNT)
  LC(LED_STATUS_START, LED_STATUS_COUNT, COL_STATUS_TEMP, LED_STATUS_COUNT)

  // Všechny LEDky bílé — check maximálního jasu
  pixels.fill(pixels.Color(255,255,255));
  pixels.show(); delay(SELFTEST_LED_COLORCHECK_MS);

  pixels.clear(); pixels.show();
  #undef LC
  Serial.println("LED color check: done");
}
#endif

void runSelfTest()
{
  Serial.println("\n=== SELF TEST ===");

  int r_ina=-1, r_inaLed=-1;
  int r_bat=-1, r_motor=-1, r_led=-1;
  int r_btn=-1, r_cover=-1, r_pot=-1;
  bool anyError=false;
  bool coverOpen=false;

  // 1. I2C scan
#if USE_I2C
  r_ina    = i2cProbe(0x40) ? 1 : 0;
  r_inaLed = i2cProbe(INA219_LED_ADDR) ? 1 : 0;
  bool dispFound = i2cProbe(0x3C) || i2cProbe(0x3D);
  Serial.print("INA219 bat  (0x40): "); Serial.println(r_ina    ?"OK":"FAIL");
  Serial.print("INA219 LED  (0x41): "); Serial.println(r_inaLed ?"OK":"not found");
  Serial.print("Display (0x3C/3D):  "); Serial.println(dispFound?"OK":"FAIL");
  if(!r_ina){ anyError=true; }
  #if USE_INA_CHARGER
  {
    bool chgInaFound = i2cProbe(INA_CHARGER_ADDR);
    Serial.print("INA_CHARGER (0x44): "); Serial.println(chgInaFound?"OK":"FAIL");
    if(!chgInaFound){ anyError=true; }
  }
  #endif
#endif

  // 2. Napětí baterie
#if USE_INA219
  if(inaOK){
    float v=ina219.getBusVoltage_V();
    r_bat=(v>=BAT_EMPTY && v<=BAT_FULL) ? 1 : 0;
    Serial.print("Battery voltage: "); Serial.print(v,2);
    Serial.print("V -> "); Serial.println(r_bat?"OK":"OUT OF RANGE");
    if(!r_bat) anyError=true;
  }
#endif

  // 3. Tlačítko
#if USE_INPUT
  bool btnAtBoot=readButton();
  r_btn=btnAtBoot ? 0 : 1;
  Serial.print("Button at boot: ");
  Serial.println(btnAtBoot?"PRESSED (stuck?)":"OK");
  if(btnAtBoot) anyError=true;
#endif

  // 4. Kryt
#if USE_COVER
  coverOpen=(digitalRead(COVER)==HIGH);
  r_cover=coverOpen ? 0 : 1;
  Serial.print("Cover: ");
  Serial.println(coverOpen?"OPEN":"closed OK");
  if(coverOpen) anyError=true;
#else
  r_cover=1; // snímač nezapojen — považujeme za zavřený
  Serial.println("Cover: skipped (USE_COVER=0)");
#endif

  // 5. Potenciometry
#if USE_POT
  int potS=analogRead(POT_SPEED);
  int potB=analogRead(POT_BEEP);
  bool potFail=(potS<=10||potS>=4085)||(potB<=10||potB>=4085);
  r_pot=1; // poty jsou jen warning, nenastavujeme chybu
  Serial.print("POT_SPEED:"); Serial.print(potS);
  Serial.print("  POT_BEEP:"); Serial.println(potB);
  if(potFail) Serial.println("POT: WARNING — possibly stuck at rail (not wired?)");
#endif

  // 6. Motor test — měří delta proudu oproti baseline (eliminuje LED + systém)
#if USE_MOTOR
  float mBase=0;
  #if USE_INA219
  if(inaOK) mBase=ina219.getCurrent_mA()/1000.0;
  #endif
  Serial.print("Motor test baseline: "); Serial.print(mBase,3); Serial.println("A");
  Serial.print("Motor test (300ms @ 80%)... ");
  motorRun(true,80);
  delay(MOTOR_TEST_DURATION);
  float mCurrent=0, mDelta=0;
  #if USE_INA219
  if(inaOK){
    mCurrent=ina219.getCurrent_mA()/1000.0;
    mDelta=mCurrent-mBase;
  }
  #endif
  motorStop();
  Serial.print("total="); Serial.print(mCurrent,3);
  Serial.print("A  delta="); Serial.print(mDelta,3); Serial.print("A -> ");
  if(mDelta < MOTOR_TEST_MIN_CURRENT){
    r_motor=0;
    Serial.println("FAIL — motor pravdepodobne odpojen");
    anyError=true;
  } else if(mCurrent > STALL_CURRENT){
    r_motor=0;
    Serial.println("FAIL — stall proud uz pri startu");
    anyError=true;
  } else {
    r_motor=1;
    Serial.println("OK");
  }
#endif

  // 7. LED test přes druhý INA219
#if USE_LED && USE_INA219_LED
  if(inaLedOK){
    Serial.print("LED test (all white)... ");
    float baseCurrent=ina219led.getCurrent_mA()/1000.0;
    pixels.fill(pixels.Color(255,255,255));
    pixels.show();
    delay(80);
    float onCurrent=ina219led.getCurrent_mA()/1000.0;
    pixels.clear();
    pixels.show();
    float delta=onCurrent-baseCurrent;
    r_led=(delta>=LED_TEST_MIN_CURRENT) ? 1 : 0;
    Serial.print("delta="); Serial.print(delta,3);
    Serial.println(r_led?"A — OK":"A — FAIL (low delta, LEDs missing?)");
    if(!r_led) anyError=true;
  }
#endif

  // 8. Flow sensor check
#if USE_FLOW
  {
    bool flowBlocked=(digitalRead(FLOW_PIN)==LOW);  // LOW = IR odraz/přerušení = objekt v cestě
    Serial.print("Flow sensor D0: ");
    Serial.println(flowBlocked ? "LOW (blocked at start!)" : "HIGH — OK");
    if(flowBlocked) Serial.println("FLOW: WARNING — sensor blocked at start, check alignment");
  }
#endif

  // 9. Beeper test
#if USE_BEEPER
  ledcWrite(pwmChanBeep,128);
  delay(100);
  ledcWrite(pwmChanBeep,0);
  Serial.println("Beeper: test tone done");
#endif

  // 10. LED color check — ladění jasu a barev (zapnout přes SELFTEST_LED_COLORCHECK=1)
#if USE_LED && SELFTEST_LED_COLORCHECK
  ledColorCheck();
#endif

  // Výsledky
  Serial.println(anyError ? "=== SELF TEST: ERRORS ===" : "=== SELF TEST: PASS ===\n");

#if USE_DISPLAY
  showSelfTestDisplay(r_ina,r_inaLed,r_bat,r_motor,r_led,r_btn,r_cover,r_pot);
  delay(2000);
#endif

  // Nastavit error state podle výsledku
  if(coverOpen){
    errorState=true;
    strcpy(errorMsg,"Cover open");
  } else if(anyError && r_motor==0){
    errorState=true;
    strcpy(errorMsg,"Motor error");
  } else if(anyError && r_bat==0){
    errorState=true;
    strcpy(errorMsg,"Battery err");
  }
}
#endif

/* =================================================
   CHARGER
================================================= */
#if USE_CHARGER

#if USE_BEEPER
void beepChargeFull()
{
  for(int i=0;i<3;i++){
    ledcWrite(pwmChanBeep,128);  // 50% duty = max hlasitost
    delay(150);
    ledcWrite(pwmChanBeep,0);
    delay(100);
  }
}
#endif

void handleChargeState()
{
  // Detekce přítomnosti a validity vstupního napětí
#if USE_INA_CHARGER
  // INA219 na vstupu CN3722: přesné napětí + rozsahová kontrola (14–24 V)
  bool chargerPresent = inaChgOK &&
    (chargerVoltage >= CHARGER_MIN_V && chargerVoltage <= CHARGER_MAX_V);
#else
  // Záložní detekce: jednoduchý voltage divider na pin 34
  bool chargerPresent = (digitalRead(CHARGE_DETECT_PIN)==HIGH);
#endif

  switch(chargeState){

    case CHARGE_DISCONNECTED:
      if(chargerPresent){
#if USE_NTC
        if(ntcTemp > CHARGE_TEMP_MAX){
          // Baterie je příliš teplá — zdroj připojen, ale nabíjení nezahájit
          DBG(1,DBG_BAT,"Charger connected but battery too hot — waiting for cool-down");
          break;
        }
#endif
        digitalWrite(CHARGE_ENABLE_PIN,HIGH);
        chargeState=CHARGE_ACTIVE;
        chargeFull_signaled=false;
        DBG(1,DBG_BAT,"Charger connected — charging started");
      }
      break;

    case CHARGE_ACTIVE:
      if(!chargerPresent){
        digitalWrite(CHARGE_ENABLE_PIN,LOW);
        chargeState=CHARGE_DISCONNECTED;
        DBG(1,DBG_BAT,"Charger disconnected");
        break;
      }
#if USE_NTC
      if(ntcTemp > CHARGE_TEMP_MAX){
        digitalWrite(CHARGE_ENABLE_PIN,LOW);
        errorState=true;
        strcpy(errorMsg,"Charge temp!");
        chargeState=CHARGE_TEMP_ERROR;
        DBG(1,DBG_BAT,"Charging stopped: over temp");
        break;
      }
#endif
      if(voltage >= BAT_FULL-0.1 && abs(current) < CHARGE_FULL_CURRENT){
        digitalWrite(CHARGE_ENABLE_PIN,LOW);
        chargeState=CHARGE_FULL;
        DBG(1,DBG_BAT,"Battery FULL");
      }
      break;

    case CHARGE_FULL:
      if(!chargerPresent){
        chargeState=CHARGE_DISCONNECTED;
        chargeFull_signaled=false;
        DBG(1,DBG_BAT,"Charger removed after full");
      }
      break;

    case CHARGE_TEMP_ERROR:
      if(!chargerPresent){
        chargeState=CHARGE_DISCONNECTED;
        errorState=false;
        strcpy(errorMsg,"OK");
      }
#if USE_NTC
      else if(ntcTemp <= CHARGE_TEMP_MAX-5.0){
        digitalWrite(CHARGE_ENABLE_PIN,HIGH);
        chargeState=CHARGE_ACTIVE;
        errorState=false;
        strcpy(errorMsg,"OK");
        DBG(1,DBG_BAT,"Charging resumed after cool-down");
      }
#endif
      break;
  }
}
#endif

/* =================================================
   SETUP
================================================= */
void setup()
{
  Serial.begin(115200);

#if USE_INPUT
  pinMode(BTN,INPUT_PULLUP);
#endif
#if USE_COVER
  pinMode(COVER,INPUT_PULLUP);
#endif
#if USE_MOTOR
  pinMode(BTN2,INPUT_PULLUP);
#endif

#if USE_MOTOR
  pinMode(R_EN,OUTPUT);
  pinMode(L_EN,OUTPUT);

  ledcSetup(pwmChanR,20000,8);
  ledcSetup(pwmChanL,20000,8);

  ledcAttachPin(RPWM,pwmChanR);
  ledcAttachPin(LPWM,pwmChanL);
#endif

#if USE_BEEPER
  ledcSetup(pwmChanBeep,BEEP_FREQ,8);
  ledcAttachPin(BEEPER,pwmChanBeep);
#endif

#if USE_CHARGER
  pinMode(CHARGE_ENABLE_PIN,OUTPUT);
  digitalWrite(CHARGE_ENABLE_PIN,LOW);
  #if !USE_INA_CHARGER
  pinMode(CHARGE_DETECT_PIN,INPUT);   // voltage divider detekce — použije se jen bez USE_INA_CHARGER
  #endif
#endif

#if USE_FLOW
  pinMode(FLOW_PIN,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN),flowISR,FALLING);
  flowLastPulseT=millis();
#endif

#if USE_I2C
  Wire.begin(21,22);
#endif

#if USE_INA219
  inaOK=ina219.begin();
#endif

#if USE_INA219_LED
  inaLedOK=ina219led.begin();
#endif

#if USE_INA_CHARGER
  inaChgOK=ina_chg.begin();
#endif

#if USE_DISPLAY
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
#endif

#if USE_LED
  pixels.begin();
  pixels.clear();
  pixels.show();
#endif

#if USE_SELFTEST
  runSelfTest();
#endif

  // Watchdog — inicializuje se po self-testu (self-test může trvat déle kvůli LED color check)
  esp_task_wdt_init(WDT_TIMEOUT_S, true);   // true = panic/reset při vypršení
  esp_task_wdt_add(NULL);                   // přihlásit hlavní task
  Serial.print("WDT: armed, timeout=");
  Serial.print(WDT_TIMEOUT_S);
  Serial.println("s");

#if USE_OTA
  Serial.print("OTA: connecting to "); Serial.print(OTA_WIFI_SSID); Serial.print("...");
  WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASS);
  {
    unsigned long wifiT=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-wifiT<15000){
      delay(250); Serial.print(".");
    }
  }
  if(WiFi.status()==WL_CONNECTED){
    Serial.print("\nOTA: WiFi OK, IP="); Serial.println(WiFi.localIP());
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.onStart([](){
      esp_task_wdt_delete(NULL);   // OTA trvá déle než WDT_TIMEOUT_S — dočasně vypnout
      Serial.println("OTA: zacina update...");
    });
    ArduinoOTA.onEnd([](){
      Serial.println("\nOTA: hotovo, restartuji...");
    });
    ArduinoOTA.onProgress([](unsigned int prog, unsigned int total){
      static unsigned long lastPrint=0;
      if(millis()-lastPrint>2000){ lastPrint=millis();
        Serial.printf("OTA: %u%%\n", prog*100/total);
      }
    });
    ArduinoOTA.onError([](ota_error_t e){
      Serial.printf("OTA chyba[%u]\n",e);
    });
    ArduinoOTA.begin();
    Serial.println("OTA: ready — pouzij Arduino IDE 'Upload via OTA'");
  } else {
    Serial.println("\nOTA: WiFi FAILED — pokracuji bez OTA");
  }
#endif

  DBG(1,1,"System init OK");
}

/* =================================================
   LOOP
================================================= */
void loop()
{

#if USE_INA219
  if(inaOK){
    voltage=ina219.getBusVoltage_V();
    current=ina219.getCurrent_mA()/1000.0;
    power=voltage*current;
  }
#endif

#if USE_INA219_LED
  if(inaLedOK){
    ledCurrent=ina219led.getCurrent_mA()/1000.0;
  }
#endif

#if USE_INA_CHARGER
  if(inaChgOK){
    chargerVoltage=ina_chg.getBusVoltage_V();
    chargerInputCurrent=ina_chg.getCurrent_mA()/1000.0;
  }
#endif

#if USE_NTC
  ntcTemp=readNTCtemp();
#endif

#if USE_POT
  // EMA (exponential moving average) — plynule čteni, eliminuje blikani LED na hranici
  // alpha=0.5: dosahne 99% za ~7 iteraci = 700ms pri delay(100)
  static float emaSpeed=(float)analogRead(POT_SPEED);
  static float emaBeep =(float)analogRead(POT_BEEP);
  emaSpeed=emaSpeed*0.5f+analogRead(POT_SPEED)*0.5f;
  emaBeep =emaBeep *0.5f+analogRead(POT_BEEP) *0.5f;
  // map: POT_MIN→0%, POT_MAX→100%; constrain ošetří přesah na krajích
  int speed=constrain(map((long)emaSpeed,POT_MIN,POT_MAX,0,100),0,100);
  int beep =constrain(map((long)emaBeep, POT_MIN,POT_MAX,0,100),0,100);
#else
  int speed=0;
  int beep=0;
#endif

#if USE_INPUT
  bool btnPressed=readButton();
  {
    static bool prevBtnDbg=false;
    if(btnPressed!=prevBtnDbg){
      Serial.println(btnPressed ? "BTN: PRESSED" : "BTN: released");
      prevBtnDbg=btnPressed;
    }
  }
#else
  bool btnPressed=false;
#endif

#if USE_MOTOR
  bool btn2Pressed=readButton2();
  {
    static bool prevBtn2Dbg=false;
    if(btn2Pressed!=prevBtn2Dbg){
      Serial.println(btn2Pressed ? "BTN2: PRESSED (manual reverse)" : "BTN2: released");
      prevBtn2Dbg=btn2Pressed;
    }
  }
#endif

#if USE_BEEPER
  if(btnPressed && !prevBtnPressed){
    if(beep >= 7){  // pod 7% beeper vůbec nezapínat (tichý režim)
      ledcWrite(pwmChanBeep,map(beep,0,100,0,128));  // pasivní bzučák: 128=50% duty=max hlasitost
      beepStart=millis();
      beepActive=true;
      Serial.print("BEEP: vol="); Serial.print(beep);
      Serial.print("%  dur="); Serial.print(BEEP_DURATION); Serial.println("ms");
    } else {
      Serial.println("BEEP: muted (vol<7%)");
    }
  }
  if(beepActive && millis()-beepStart>=BEEP_DURATION){
    ledcWrite(pwmChanBeep,0);
    beepActive=false;
  }
  prevBtnPressed=btnPressed;
#endif

#if USE_COVER
  if(digitalRead(COVER)==HIGH){
    errorState=true;
    strcpy(errorMsg,"Cover open");
  } else if(strcmp(errorMsg,"Cover open")==0){
    errorState=false;
    strcpy(errorMsg,"OK");
  }
#endif

#if USE_CHARGER
  handleChargeState();
  #if USE_BEEPER
  if(chargeState==CHARGE_FULL && !chargeFull_signaled){
    chargeFull_signaled=true;
    beepChargeFull();
  }
  #endif
#endif

#if USE_MOTOR
  handleMotorState(speed,btnPressed,btn2Pressed);

  // Monitorování průtoku granulí při krmení
#if USE_FLOW
  {
    static bool flowMonActive=false;
    if(motorState==MOTOR_FORWARD){
      if(!flowMonActive){
        flowMonActive=true;
        flowLastPulseT=millis();  // start okna — první puls musí přijít do FLOW_NO_PULSE_TIMEOUT
        flowPulseCount=0;
        hopperEmpty=false;
      }
      if(!hopperEmpty && (millis()-flowLastPulseT > FLOW_NO_PULSE_TIMEOUT)){
        hopperEmpty=true;
        errorState=true;
        strcpy(errorMsg,"Hopper empty");
        Serial.println("!!! FLOW: zadne granule — zasobnik prazdny nebo ucpany!");
      }
    } else {
      if(flowMonActive){
        flowMonActive=false;
        // Auto-reset: po zastaveni motoru odblokuj znovu (uzivatel doplni zasobnik)
        if(hopperEmpty && strcmp(errorMsg,"Hopper empty")==0){
          hopperEmpty=false;
          errorState=false;
          strcpy(errorMsg,"OK");
        }
      }
    }
  }
#endif

  // Výpis stavu čekání (každé 2s, jen v IDLE bez chyby)
  if(motorState==MOTOR_IDLE){
    static unsigned long waitT=0;
    if(millis()-waitT>=2000){
      waitT=millis();
      Serial.print("[ waiting ]  spd="); Serial.print(speed); Serial.print("%");
#if USE_INA219
      if(inaOK){
        Serial.print("  U="); Serial.print(voltage,1); Serial.print("V");
        Serial.print("  I="); Serial.print(current,3); Serial.print("A");
      }
#endif
#if USE_INA_CHARGER
      if(inaChgOK){
        Serial.print("  Uchg="); Serial.print(chargerVoltage,1); Serial.print("V");
      }
#endif
      if(errorState){ Serial.print("  !! ERR: "); Serial.print(errorMsg); }
      Serial.println();
    }
  }
#endif

#if USE_LED
  pixels.clear();
  updateBatteryLED(voltage);
  updatePotLED(speed,LED_SPEED_START,LED_SPEED_COUNT,pixels.Color(COL_POT_SPEED));
  updatePotLED(beep,LED_BEEP_START,LED_BEEP_COUNT,pixels.Color(COL_POT_BEEP));
  updateStatusLED();
  pixels.show();
#endif

#if USE_DISPLAY
  updateDisplay(speed,beep);
#endif

#if USE_OTA
  ArduinoOTA.handle();
#endif

  esp_task_wdt_reset();   // krmit watchdog — loop běží, vše OK
  delay(100);
}
