#include <Wire.h>
#include <Adafruit_INA219.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>

/* =================================================
   FEATURE CONFIG
================================================= */

#define USE_MOTOR     0
#define USE_BEEPER    0
#define USE_LED       0
#define USE_DISPLAY   1
#define USE_INA219    1
#define USE_INPUT     1
#define USE_POT       1

#define USE_I2C (USE_INA219 || USE_DISPLAY)


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

#define BTN_INVERT 1
#define BEEPER 5
#define NEO_PIN 27

/* =================================================
   LED MAP
================================================= */

#define LED_BAT_COUNT 3
#define LED_SPEED_COUNT 3
#define LED_BEEP_COUNT 3
#define LED_STATUS_COUNT 9

#define LED_BAT_START 0
#define LED_SPEED_START (LED_BAT_START + LED_BAT_COUNT)
#define LED_BEEP_START  (LED_SPEED_START + LED_SPEED_COUNT)
#define LED_STATUS_START (LED_BEEP_START + LED_BEEP_COUNT)

#define NUM_PIXELS (LED_BAT_COUNT + LED_SPEED_COUNT + LED_BEEP_COUNT + LED_STATUS_COUNT)

/* =================================================
   CONFIG
================================================= */

#define STALL_CURRENT 5.0
#define REVERSE_TIME 500
#define MAX_STALL_RETRIES 2

#define BAT_FULL 16.8
#define BAT_EMPTY 11.0

#define BEEP_DURATION 200

/* =================================================
   OBJEKTY
================================================= */

#if USE_INA219
Adafruit_INA219 ina219;
bool inaOK=false;
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

bool errorState=false;
char errorMsg[32] = "OK";

#if USE_MOTOR
unsigned long stallTimer=0;
unsigned long reverseStart=0;
int stallRetryCount=0;

enum MotorState{
  MOTOR_IDLE,
  MOTOR_FORWARD,
  MOTOR_REVERSE_RETRY,
  MOTOR_ERROR
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

void handleMotorState(int speedPercent,bool btnPressed)
{
  switch(motorState){

    case MOTOR_IDLE:
      if(btnPressed && !errorState){
        stallRetryCount=0;
        motorRun(true,speedPercent);
        motorState=MOTOR_FORWARD;
      }
      break;

    case MOTOR_FORWARD:

      if(!btnPressed){
        motorStop();
        motorState=MOTOR_IDLE;
        break;
      }

      if(current>STALL_CURRENT && speedPercent>20){

        if(stallTimer==0) stallTimer=millis();

        if(millis()-stallTimer>300){
          motorStop();
          motorRun(false,40);
          reverseStart=millis();
          motorState=MOTOR_REVERSE_RETRY;
        }

      }else stallTimer=0;

      break;

    case MOTOR_REVERSE_RETRY:

      if(millis()-reverseStart>REVERSE_TIME){

        motorStop();
        stallRetryCount++;

        if(stallRetryCount>=MAX_STALL_RETRIES){
          errorState=true;
          strcpy(errorMsg,"Feeding error");
          motorState=MOTOR_ERROR;
        }else{
          motorRun(true,speedPercent);
          motorState=MOTOR_FORWARD;
        }
      }
      break;

    case MOTOR_ERROR:
      motorStop();
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
  float percent=(v-BAT_EMPTY)/(BAT_FULL-BAT_EMPTY);
  percent=constrain(percent,0,1);

  int level=round(percent*LED_BAT_COUNT);

  uint32_t color;

  if(percent>0.6) color=pixels.Color(0,255,0);
  else if(percent>0.3) color=pixels.Color(255,150,0);
  else color=pixels.Color(255,0,0);

  drawBarLED(LED_BAT_START,LED_BAT_COUNT,level,color);
}

void updatePotLED(int percent,int start,int count)
{
  int level=percent/(100/count);
  drawBarLED(start,count,level,pixels.Color(0,0,255));
}

void updateStatusLED()
{
  uint32_t color=errorState ?
    pixels.Color(255,0,0) :
    pixels.Color(0,255,0);

  drawBarLED(LED_STATUS_START,LED_STATUS_COUNT,
             LED_STATUS_COUNT,color);
}
#endif

/* =================================================
   DISPLAY
================================================= */
#if USE_DISPLAY
void updateDisplay(int speed,int beep)
{
  u8g2.clearBuffer();
  

  u8g2.setCursor(0,10); u8g2.print("U:"); u8g2.print(voltage);
  u8g2.setCursor(0,20); u8g2.print("I:"); u8g2.print(current);
  u8g2.setCursor(0,30); u8g2.print("P:"); u8g2.print(power);
  u8g2.setCursor(0,40); u8g2.print("Speed:"); u8g2.print(speed);
  u8g2.setCursor(0,50); u8g2.print("Beep:"); u8g2.print(beep);

#if USE_MOTOR
  u8g2.setCursor(0,60);
  u8g2.print(errorMsg);
#endif

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
   POT
================================================= */
int getStepPercent(int adc)
{
  int step=map(adc,0,4095,0,5);
  return step*20;
}

/* =================================================
   SETUP
================================================= */
void setup()
{
  Serial.begin(115200);

#if USE_INPUT
  pinMode(BTN,INPUT_PULLUP);
  pinMode(COVER,INPUT_PULLUP);
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
  ledcSetup(pwmChanBeep,4000,8);
  ledcAttachPin(BEEPER,pwmChanBeep);
#endif

#if USE_I2C
  //Wire.begin(21,22);
#endif

#if USE_INA219
  //Wire.begin(21,22);
  inaOK = ina219.begin();
#endif

#if USE_DISPLAY
  //Wire.begin(21,22);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);	// choose a suitable font
#endif

#if USE_LED
  pixels.begin();
  pixels.clear();
  pixels.show();
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

#if USE_POT
  int speed=getStepPercent(analogRead(POT_SPEED));
  int beep=getStepPercent(analogRead(POT_BEEP));
#else
  int speed=0;
  int beep=0;
#endif

#if USE_INPUT
  bool btnPressed=readButton();
#else
  bool btnPressed=false;
#endif

#if USE_BEEPER
  if(btnPressed && !prevBtnPressed){
    ledcWrite(pwmChanBeep,map(beep,0,100,0,255));
    beepStart=millis();
    beepActive=true;
  }
  if(beepActive && millis()-beepStart>=BEEP_DURATION){
    ledcWrite(pwmChanBeep,0);
    beepActive=false;
  }
  prevBtnPressed=btnPressed;
#endif

#if USE_INPUT
  if(digitalRead(COVER)==HIGH){
    errorState=true;
    strcpy(errorMsg,"Cover open");
  } else if(strcmp(errorMsg,"Cover open")==0){
    errorState=false;
    strcpy(errorMsg,"OK");
  }
#endif

#if USE_MOTOR
  handleMotorState(speed,btnPressed);
#endif

#if USE_LED
  pixels.clear();
  updateBatteryLED(voltage);
  updatePotLED(speed,LED_SPEED_START,LED_SPEED_COUNT);
  updatePotLED(beep,LED_BEEP_START,LED_BEEP_COUNT);
  updateStatusLED();
  pixels.show();
#endif

#if USE_DISPLAY
  updateDisplay(speed,beep);
#endif

  delay(100);
  DBG3(3,1,".");
}
