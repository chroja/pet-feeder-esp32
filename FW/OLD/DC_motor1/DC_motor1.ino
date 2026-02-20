/* BTS7960 Motor Test - ESP32 Version */

const int RPWM = 16;
const int R_EN = 17;
const int LPWM = 18;
const int L_EN = 19;
const int BEEPER = 5 ;
const int BTN = 13;
const int NEO_PIN = 27;
const int SDA = 21;
const int SCL = 22;
const int COVER = 33;
const int POT_SPEED = 25;
const int POT_BEEP = 26
const int BTN_2 = 14;


int BEEP_PWM = 255;


bool inv_BTN = 1;




bool FEED = false;
bool PREV_BTN = true;

int pwmChanR = 0;
int pwmChanL = 1;
int pwmBeep = 2;

void manualRotateWhilePressed() {

  digitalWrite(BEEPER, HIGH);
  delay(150);
  digitalWrite(BEEPER, LOW);

  Serial.println("Manual rotation START");

  digitalWrite(L_EN, HIGH);
  digitalWrite(R_EN, HIGH);   // povolit driver

  for (int i = 0; i < 256; i++) {
    ledcWrite(pwmChanR, i);   // doprava
    ledcWrite(pwmChanL, 0);
    delay(2);
  }

  if (inv_BTN > 0){
    while (digitalRead(BTN) == HIGH) {
      delay(50);               // krátká pauza na kontrolu tlačítka
    }
  } else{
    while (digitalRead(BTN) == LOW) {
      delay(50);               // krátká pauza na kontrolu tlačítka
    }
  }

  
  
  for (int i = 255; i >= 0; i--) {
    ledcWrite(pwmChanR, i);
    ledcWrite(pwmChanL, 0);
    delay(2);
  }

  // STOP
  ledcWrite(pwmChanR, 0);
  ledcWrite(pwmChanL, 0);
  digitalWrite(L_EN, LOW);
  digitalWrite(R_EN, LOW);

#ifdef USE_LED
  digitalWrite(LED, LOW);
#endif

  Serial.println("Manual rotation STOP");
}





void setup() {
  Serial.begin(115200);

  pinMode(L_EN, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(BEEPER, OUTPUT);
  pinMode(R_LED, OUTPUT);
  pinMode(G_LED, OUTPUT);
  pinMode(B_LED, OUTPUT);

  digitalWrite(L_EN, LOW);
  digitalWrite(R_EN, LOW);
  digitalWrite(BEEPER, LOW);

  // PWM setup
  ledcSetup(pwmChanR, 20000, 8);
  ledcSetup(pwmChanL, 20000, 8);
  //ledcSetup(pwmBeep, 5000, 8);

  ledcAttachPin(RPWM, pwmChanR);
  ledcAttachPin(LPWM, pwmChanL);
  //ledcAttachPin(BEEP, pwmBeep);
  digitalWrite(R_LED, HIGH);
  delay (500);
  digitalWrite(R_LED, LOW);
  digitalWrite(G_LED, HIGH);
  delay (500);
  digitalWrite(G_LED, LOW);
  digitalWrite(B_LED, HIGH);
  delay (500);
  digitalWrite(B_LED, LOW);

  //delay(1000);
}

void loop() {

  bool btnNow = digitalRead(BTN);
  if (inv_BTN > 0){
    btnNow = !btnNow;
  }

  // TLAČÍTKO PRÁVĚ STISKNUTO
  if (btnNow == LOW && PREV_BTN == HIGH) {
    manualRotateWhilePressed();
  }

  PREV_BTN = btnNow;





/*
  // ============================================
  // 1) SMĚR DOPRAVA
  // ============================================
  Serial.println("SMER: DOPRAVA");

  digitalWrite(L_EN, HIGH);
  digitalWrite(R_EN, HIGH);   // povolit driver

  for (int i = 0; i < 256; i++) {
    ledcWrite(pwmChanR, i);   // doprava
    ledcWrite(pwmChanL, 0);
    delay(2);
  }
  delay(2000);
  for (int i = 255; i >= 0; i--) {
    ledcWrite(pwmChanR, i);
    ledcWrite(pwmChanL, 0);
    delay(2);
  }

  // STOP
  ledcWrite(pwmChanR, 0);
  ledcWrite(pwmChanL, 0);
  digitalWrite(L_EN, LOW);
  digitalWrite(R_EN, LOW);
  //ledcWrite(pwmBeep, BEEP_PWM);
  delay(500);
  //ledcWrite(pwmBeep, 0);

  // ============================================
  // 2) SMĚR DOLEVA
  // ============================================
  Serial.println("SMER: DOLEVA");

  digitalWrite(L_EN, HIGH);
  digitalWrite(R_EN, HIGH);   // povolit driver

  for (int i = 0; i < 256; i++) {
    ledcWrite(pwmChanL, i);   // doleva
    ledcWrite(pwmChanR, 0);
    delay(2);
  }
  delay(2000);
  for (int i = 255; i >= 0; i--) {
    ledcWrite(pwmChanL, i);
    ledcWrite(pwmChanR, 0);
    delay(2);
  }

  // STOP
  ledcWrite(pwmChanR, 0);
  ledcWrite(pwmChanL, 0);
  digitalWrite(L_EN, LOW);
  digitalWrite(R_EN, LOW);
  digitalWrite(BEEP, HIGH);
  delay(500);
  digitalWrite(BEEP, LOW);
*/
}
