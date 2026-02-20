/*************************************************************
Motor Shield X-NUCLEO-IHM14A1 Stepper Demo with STSPIN820 controller for MKR1000
By JV / 2019

Nem42 stepper motor - 1.8 degrees/step, BiPol (4 lead), max 0.4Amp per coil (37Ohm av resustance = high) -> Sense : 1.65V/Amp => 3.3V / 2 amp
Motor Driver STSPIN820 on IHM14A1 Motor Shield 

Function: start stepper left or right by defined stepper Mode
Runs for a cerctain amount of usteps
Shows interrupt Flag.

There is no way to measure the Motor Current with this board (unfortunatly), but easy of drive without software-clocking is very nice.

*************************************************************/
#include <Arduino.h>
#include "driver/rmt_tx.h"

#define  DBG
#define  FULL_STEP 0
#define  HALF_STEP 1
#define  QUAD_STEP 2
#define  OCTA_STEP 3
#define  SIXT_STEP 4
// Define more for 1/32 and 1/64  ,1/28 or 1/256 if you like, (see Datasheet 5.2 page 13)
#define  CW 1
#define  CCW 0

const int STBY=-1;     // STBY = Standby\reset input. When forced low the device enters in low consumption mode: all motor-coils are non-current 
const int REF=-1;      // REF = Reference voltage for the PWM current control circuitry. should be 0 for max Vref or PWM-ed (turn R7 pot to 1K)
const int MODE1=19;    // MODE = stepper setting : [M1,M2,M3] = 0,0,0 = full step, 1,0,0 half step, 0,1,0 quartestep etc  -  (see Datasheet 5.2 page 13) Remark : D13 on UNO !!
const int MODE2=18;   // 
const int MODE3=5;   // 
const int EN_FAULT=21; // EN_FAULT = input for failure, or forced output . This is the power stage enable (when low, the power stage is turned off) and is forced low through the integrated open-drain MOSFET when a failure occurs.
const int INT_FAULT=-1;// EN_FAULT is hard wired to MKR pin 1  for interrupt -> pin 2 does not serve interupt on MKR boards !! (MKR Pin1 = UNO2MKR pinD10 / PA23_TC4-W1)
const int STCK=23;     // STCK = Step clock input, ie 1KHz, used vie tone() command
const int DECAY=-1;    // DECAY = Decay mode selection input. High logic level sets slow decay mode; low logic level sets mixed decay mode - (see Section 5.3 on page 16 Datasheet).
const int DIR =22;     // DIR = Direction input
int INT_FLAG=0;




#define  STEP1_128 6

int MOTOR_STEPS = 200;
int MICROSTEP = 128;
int MICROSTEP_PER_REV = MOTOR_STEPS * MICROSTEP;
int FREQ = 500 * MICROSTEP;

rmt_channel_handle_t rmt_chan = NULL;

void setup() {
  
  //establish motor driver pins
  pinMode(STBY, OUTPUT);  
  pinMode(REF, OUTPUT);   
  pinMode(MODE1, OUTPUT);   pinMode(MODE2, OUTPUT);   pinMode(MODE3, OUTPUT);
  pinMode(EN_FAULT, INPUT); // set to monitor Fault
  pinMode(STCK, OUTPUT);
  pinMode(DECAY, OUTPUT);
  pinMode(DIR, OUTPUT);
  pinMode(INT_FAULT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_FAULT), Fault, FALLING);
  Serial.begin(9600); 
  Serial.println("\nMotor Shield X-NUCLEO-IHM14A1 Stepper Demo with STSPIN820 controller for MKR1000");
 
}

void loop(){ // Example loops

delay(2000);

INT_FLAG=0;
Serial.println("\nStart Motor Loop:");

MotorLoop(500*128,200*128,CW,6);  // (0.5KHz) 2ms step-time, 1600 steps = 4 turns @ 1/2step  (200 fullsteps @ 1.8degree = full turn)

//delay(6000);
Serial.print("\npuvodni");
//MotorPowerDown();
Serial.print("\ndeg");
delay(5000);


int DEG_FOR_FEED = 180;
MotorDeg(FREQ, DEG_FOR_FEED, STEP1_128);
delay(5000);


}

// interrupt routine for EN_FAULT going low
void Fault() {
   INT_FLAG=!INT_FLAG;
}


// motor start By setting tone to the STspin820 clock input for a certain time
// Freq - Clock freq, stp = stepper steps, so time (in ms) = 1000*stp/frq. 
// stp=0 : no time limit, run forever
void MotorLoop( int frq,int stp,int dir,byte mod) {

digitalWrite(STBY,1);  // Wake up Controller
digitalWrite(MODE1,mod&1); // bit0 = mode1
digitalWrite(MODE2,mod&2); // bit1 = mode2
digitalWrite(MODE3,mod&4); // bit3 = mode3
digitalWrite(DIR,dir);  // set direction
delay(10);
if (stp != 0) tone(STCK, (unsigned int) frq, (unsigned long) (1000*stp)/frq ); // turn on for amount of step-time
else tone(STCK, (unsigned int) frq ); // turn on, no time limit
}



// power down the motor, set in standby
void MotorPowerDown() {
digitalWrite(STBY,0); // Forced to standby
digitalWrite(STCK,0); // clock zero  
}

/*
void MotorInit() {
digitalWrite(STBY,0);  // Forced to standby
digitalWrite(STCK,0);  // clock zero
digitalWrite(REF,0);   // Reference PWM is Zero: R7 pot makes the Vref
digitalWrite(DECAY,1); // Mixed Decay mode
}
*/
/*

void MotorDeg( int frq,int deg, byte mod) {
  Serial.println("MotorDeg");
  bool dir;
  if (deg > 0) {
    dir = CW;
    Serial.println("selected CW rotation");
  }
  else if (deg < 0) {
    dir = CCW;
    Serial.println("selected CCW rotation");
  }
  else{
    dir = CW;
  }
  digitalWrite(STBY,1);  // Wake up Controller
  digitalWrite(MODE1,mod&1); // bit0 = mode1
  digitalWrite(MODE2,mod&2); // bit1 = mode2
  digitalWrite(MODE3,mod&4); // bit3 = mode3
  digitalWrite(DIR,dir);  // set direction
  delay(10);
  int stp = MICROSTEP_PER_REV / 360 * deg;
  Serial.println(stp);
  if (stp != 0) tone(STCK, (unsigned int) frq, (unsigned long) (1000*stp)/frq ); // turn on for amount of step-time
  else tone(STCK, (unsigned int) frq ); // turn on, no time limit
}
*/

void MotorInit() {
   /* pinMode(DIR_PIN, OUTPUT);
    pinMode(MODE1, OUTPUT);
    pinMode(MODE2, OUTPUT);
    pinMode(MODE3, OUTPUT);
    pinMode(STBY, OUTPUT);
*/
    // RMT konfigurace
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = STCK,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .mem_block_symbols = 64,
        .resolution_hz = 1 * 1000 * 1000, // 1 MHz → jednotka = 1 µs
        .trans_queue_depth = 4,
    };
    rmt_new_tx_channel(&tx_chan_config, &rmt_chan);
    rmt_enable(rmt_chan);
}


//-------------------------------------------------------------
// MOTOR DEG
//-------------------------------------------------------------
void MotorDeg(int freq, int deg, byte microstep_mode) {
    bool dir = (deg >= 0);
    deg = abs(deg);

    digitalWrite(STBY, HIGH);

    // Nastavení směru
    digitalWrite(DIR_PIN, dir ? HIGH : LOW);

    // Nastavení microsteppingu
    digitalWrite(MODE1, (microstep_mode & 1) ? HIGH : LOW);
    digitalWrite(MODE2, (microstep_mode & 2) ? HIGH : LOW);
    digitalWrite(MODE3, (microstep_mode & 4) ? HIGH : LOW);

    // Výpočet kroků
    int steps = (MICROSTEP_PER_REV * deg) / 360;

    // Délka pulzu pro danou frekvenci
    // period = 1/freq
    // půl perioda = 500000 / freq v mikrosekundách (1 MHz rozlišení)
    int half_period = 500000 / freq; // µs

    // Vytvoření pulzu (HIGH + LOW)
    rmt_symbol_word_t pulse;
    pulse.level0 = 1;
    pulse.duration0 = half_period;
    pulse.level1 = 0;
    pulse.duration1 = half_period;

    rmt_transmit_config_t tx_conf = {
        .loop_count = steps,   // přesný počet kroků!
    };

    rmt_transmit(rmt_chan, &pulse, sizeof(pulse), &tx_conf);

    // Počkat na dokončení
    rmt_tx_wait_all_done(rmt_chan, portMAX_DELAY);

    digitalWrite(STBY, LOW); // uspání driveru (volitelné)
}
}