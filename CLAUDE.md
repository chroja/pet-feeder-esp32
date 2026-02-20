# Pet Feeder ESP32 — projektové poznámky

## Aktivní firmware
Pracujeme výhradně na `FW/petfw_na_baterii/petfw_na_baterii.ino`.
Ostatní soubory v repozitáři jsou starší experimenty — neměnit.
Detailní plán PCB: viz `pcb_roadmap.md`.

## Hardware

### MCU
- ESP32 (3.3V logika)

### Baterie
- GEB 18650 4S1P, nominální 14.8V, plná 16.8V, minimum 11.3V (`BAT_EMPTY 11.3`)
- Kapacita 3200mAh, konektor JST-PH-2, max. proud konektorem **2A**
- Interní BMS — na PCB stačí jen CC/CV nabíjecí obvod
- Nabíjení: 640mA (0.2C) doporučeno, max 1600mA (0.5C), teplota 0–45°C

### Napájecí architektura (finální PCB)
```
Baterie (11.3–16.8V) → Polyfuse 2A → INA226 #1 (0x40) ──┬── AP63300 → 10V → INA226 #2 (0x41) → DRV8874 → Motor
                                                           ├── AP63300 →  5V → INA226 #3 (0x42) → LEDky + Beeper
                                                           └── AP63300 → 3.3V → INA226 #4 (0x43) → ESP32, logika
```
- **Motor z regulovaných 10V** (ne přímo z baterie) — stabilní otáčky, motor vždy v spec
- **4× INA226** (stejný čip, různé I2C adresy) — monitoring každé větve bez extra GPIO
- **5V větev:** LED + Beeper (MOSFET budič z 5V) — obojí testovatelné self-testem přes INA226 #3
- INA226 adresy: A0/A1 strapping (GND=0x40, VS=0x41, SDA=0x42, SCL=0x43)

### Prototyp na stole (aktuální stav)
| Komponenta | Stav |
|---|---|
| H-bridge BTS7960B (IBT-2) | Funkční, overkill (43A) |
| Motor JGY-370 12V | Funkční |
| Buck MP1584EN → 11V (motor) | Funkční |
| Buck MP1584EN → 3.3V (kaskáda z 11V!) | Funkční, nevhodné |
| Buck MP1584EN → 5V (LED) | Funkční |
| INA219 @ 0x40 (bateriová větev) | Funkční |
| 24× WS2812B testovací matice | Funkční (kód chce 40 LED) |
| Beeper KY-006 pasivní 2kHz | Funkční, tichý (bez MOSFETu) |

### Motor
- JGY-370 12V, H-bridge BTS7960B → plánovaná náhrada **DRV8874** (3.5A, AIPROPI proudový výstup)
- `STALL_CURRENT 0.15A` — změřit reálně!

### Beeper
- Pasivní KY-006 (2 kHz), řízen 2N7000 MOSFET z 5V (plánováno)
- `BEEP_FREQ 2000` Hz, 50% duty = max hlasitost, `map(beep, 0, 100, 0, 128)` — max duty 128!
- **Ztlumení:** `beep < 7%` → beeper se vůbec nespustí

### NeoPixel LEDs
- SK6812 doporučeno (3.3V data OK), nebo WS2815 (12V), nebo WS2812B + 74AHCT125 (level shifter)
- Na stole: 24× WS2812B, kód vyžaduje 40 pixelů
- **LED mapa:** BAT(8) + SPEED(8) + BEEP(8) + STATUS(16) = 40 pixelů

## Pinout ESP32
| Pin | Funkce | Typ |
|-----|--------|-----|
| 4  | FLOW_PIN — TCRT5000 D0 (USE_FLOW) | INPUT_PULLUP / ISR |
| 5  | BEEPER (PWM ch2, 2000Hz) | OUTPUT |
| 13 | BTN — RF ch1, krmení | INPUT_PULLUP |
| 14 | BTN2 — RF ch2, zpětný chod | INPUT_PULLUP |
| 16 | RPWM (PWM ch0, 20kHz) | OUTPUT |
| 17 | R_EN / nSLEEP | OUTPUT |
| 18 | LPWM (PWM ch1, 20kHz) | OUTPUT |
| 19 | L_EN / FAULT | OUTPUT |
| 21 | I2C SDA | I2C |
| 22 | I2C SCL | I2C |
| 23 | DRV8874 nSLEEP (volný GPIO) | OUTPUT |
| 25 | POT_SPEED (ADC) | INPUT ADC |
| 26 | POT_BEEP (ADC) | INPUT ADC |
| 27 | NEO_PIN (NeoPixel data) | OUTPUT |
| 32 | CHARGE_ENABLE (USE_CHARGER) | OUTPUT |
| 33 | COVER (USE_COVER=0) | INPUT_PULLUP |
| 34 | CHARGE_DETECT *(opt., záloha bez USE_INA_CHARGER)* | INPUT ONLY |
| 35 | NTC_PIN (INPUT ONLY ADC) | INPUT ADC |
| 36 | AIPROPI z DRV8874 (INPUT ONLY ADC) | INPUT ADC |

**Bootstrap (nepoužívat pro vstupy s pull):** 0, 2, 12, 15
**Nepoužívat:** 1 (TX), 3 (RX), 6–11 (SPI Flash)

## Feature flags — aktuální stav
```
USE_MOTOR      1    USE_BEEPER     1    USE_LED        1
USE_DISPLAY    1    USE_INA219     1    USE_INA219_LED 0
USE_INPUT      1    USE_COVER      0    USE_POT        1
USE_CHARGER    0    USE_NTC        0    USE_SELFTEST   1
USE_FLOW       0    USE_OTA        0
```

## Display — layout (updateDisplay)
```
┌────────────────────────────────┐
│ U:14.8V  I:0.12A  P:1.8W      │  ← font 6×10, y=10, x=0/44/90
│ Spd:75  Bp:50  Bat:72%        │  ← font 6×10, y=22, x=0/42/80
├────────────────────────────────┤  ← drawHLine y=25
│                                │
│            FEED                │  ← font 10×20 bold, centrovaný
│                                │
└────────────────────────────────┘
```
- **Bat%** = lineární odhad z napětí: `(U - 11.3) / (16.8 - 11.3) × 100`
- **Velký stav** — priorita: motor akce > chyba > nabíjení > OK

| Stav | Text | Font |
|------|------|------|
| Čekání | `OK` | 10×20 |
| Krmení | `FEED` | 10×20 |
| Uvolňování | `UNJAM` | 10×20 |
| Ruční zpětný chod | `REV` | 10×20 |
| Nabíjení | `CHRG` | 10×20 |
| Nabito | `FULL` | 10×20 |
| Chyba (errorMsg) | `Feeding error` apod. | 7×13B |

## LED mapa a barvy
```
LED_BAT_COUNT 8 / LED_SPEED_COUNT 8 / LED_BEEP_COUNT 8 / LED_STATUS_COUNT 16 = 40 px
```
Baterie chování:
- 0–9%: dýchá červeně (sinus ~2s, jas 30–255)
- 10–34%: 2 LED COL_BAT_LOW / 35–69%: 4 LED COL_BAT_MID / 70–89%: 6 LED COL_BAT_HIGH / 90–100%: 8 LED COL_BAT_FULL
- Přehřátí (NTC): bliká 1Hz COL_BAT_NTC / Nabíjení: dýchá zeleně 30–230 / plné: COL_BAT_CHG_FULL

Barvy `COL_*` jsou `#define R, G, B` — aktuální hodnoty jsou záměrně tlumené (max 150 místo 255).
`updatePotLED(percent, start, count, color)` — barva jako parametr.
`updateStatusLED()` — používá COL_STATUS_OK/ERR/CHG/FULL/TEMP.

## Klíčová rozhodnutí

### Čtení potenciometrů
EMA alpha=0.5 (~700ms na 99%): `ema = ema*0.5 + adc*0.5`
`constrain(map(ema, POT_MIN, POT_MAX, 0, 100), 0, 100)` — `POT_MIN 150`, `POT_MAX 3945`

### Detekce zaseknutého motoru
IDLE → FORWARD → REVERSE_RETRY → ERROR → MANUAL_REVERSE → IDLE
- Stall: `STALL_CURRENT 0.15A` po >300ms → zpětný chod 500ms
- Po `MAX_STALL_RETRIES=2` → errorState, čeká BTN2
- **BTN2 při MOTOR_ERROR:** držení = ruční zpětný chod, uvolnění = reset + IDLE

### Flow senzor TCRT5000 (USE_FLOW)
- FALLING ISR pin 4, `volatile flowPulseCount`, `flowLastPulseT`
- >3s bez pulzu při MOTOR_FORWARD → `errorMsg="Hopper empty"`, auto-reset po zastavení

### OTA (USE_OTA)
`OTA_WIFI_SSID/PASS/HOSTNAME` → `ArduinoOTA.handle()` v loop

### Error handling
`errorState` + `errorMsg[32]` — Cover: auto-reset po zavření, Motor: přes BTN2, Hopper: auto-reset

### Self-test při startu (USE_SELFTEST=1)
1. I2C scan (0x40 INA, 0x41 INA LED, 0x3C displej)
2. Napětí baterie — ERROR mimo rozsah
3. BTN — WARNING pokud stisknuté
4. COVER — ERROR pokud otevřený (USE_COVER=1)
5. POT — WARNING pokud stuck na railu
6. Motor test: `MOTOR_TEST_DURATION 50`ms @ 80%, delta > `MOTOR_TEST_MIN_CURRENT 0.005`A
7. LED test (USE_INA219_LED): delta proud při bílé barvě
8. Flow senzor (USE_FLOW): D0 HIGH = OK
9. Beeper: krátký tón
10. **LED color check** (SELFTEST_LED_COLORCHECK=1): problikne všechny stavy pro ladění jasu/barev

**Debug define:** `SELFTEST_LED_COLORCHECK 0` — zapnout na 1 pro ladění barev
**Trvání:** `SELFTEST_LED_COLORCHECK_MS 500` ms na stav
**Sekvence:** BAT(LOW→MID→HIGH→FULL) → SPEED → BEEP → STATUS(OK→ERR→CHG→FULL→TEMP) → vše bílé

### Watchdog timer
- `#include "esp_task_wdt.h"`, `#define WDT_TIMEOUT_S 10`
- Init: `esp_task_wdt_init(WDT_TIMEOUT_S, true)` + `esp_task_wdt_add(NULL)` v setup() **po** runSelfTest()
- Krmení: `esp_task_wdt_reset()` na konci každé iterace loop() (před delay(100))
- OTA: `esp_task_wdt_delete(NULL)` v `ArduinoOTA.onStart()` — OTA trvá déle než timeout
- Self-test spouštěn před inicializací WDT → může trvat jakkoli dlouho (LED color check ~6s)

### Nabíjení (USE_CHARGER)
CN3722 + TPD63A02 (USB-C PD 20V) + barrel jack → OR-ing → **INA226 #5** → CN3722 → baterie
- **CHARGE_DETECT pin 34**: záložní voltage divider (100kΩ + 10kΩ); při `USE_INA_CHARGER=1` se nepoužívá
- **USE_INA_CHARGER**: INA219/INA226 @ 0x44 na vstupu CN3722 — přesné vstupní napětí + proud
  - `chargerPresent` = `chargerVoltage >= CHARGER_MIN_V(14V) && <= CHARGER_MAX_V(24V)`
  - Rozliší: 0V (nic), 12V (špatný adaptér), 17V (barrel jack OK), 20V (USB-C PD OK)
- CHARGE_ENABLE: pin 32 → CN3722 EN, **100kΩ pull-down na GND** (ne pull-up!)
- **Nabíjení vyžaduje zapnutý vypínač** — bez ESP32 je EN=LOW → CN3722 disabled → žádné nabíjení bez NTC ochrany
- Stavový automat: DISCONNECTED → ACTIVE → FULL / TEMP_ERROR
- Konec nabíjení: voltage ≥ 16.7V && current < 64mA
- **NTC pre-check:** před přechodem DISCONNECTED→ACTIVE se ověří teplota (USE_NTC)

### INA226 — nová architektura (plánovaná, viz pcb_roadmap.md)
5× stejný čip, 5× I2C adresa, 0 extra GPIO:
- 0x40 BAT (10mΩ) / 0x41 Motor 10V (50mΩ) / 0x42 5V LED+Beeper (100mΩ) / 0x43 3.3V (200mΩ) / **0x44 Charger in (100mΩ)**
- Adresování: A1=GND pro 0x40–0x43 (A0=GND/VS/SDA/SCL), A1=VS A0=GND pro 0x44
- Firmware: `Adafruit_INA226` nahradí `Adafruit_INA219`, nyní kód používá `USE_INA_CHARGER` s `Adafruit_INA219`
- Self-test: beeper přes INA #3, charger vstup přes INA #5 (info, ne error pokud odpojen)
