# PCB Roadmap — Pet Feeder ESP32

> Dokument pro návrh finální DPS. Obsahuje napájecí architekturu, pinout, seznam komponent a upozornění.
> Aktualizuj spolu s CLAUDE.md při každé změně HW.

---

## 1. Napájecí architektura

### Blokové schéma

```
╔══════════════════════════════════════════════════════════════════╗
║  NAPÁJENÍ — vstupní strana                                       ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                  ║
║  [USB-C Type-C]    [DC Barrel Jack 5.5/2.1 mm]                  ║
║  TPD63A02           ~17–20 V přímý vstup                         ║
║  (žádá 20 V PD)         │                                        ║
║       │                 │                                        ║
║       └──[OR-ing Schottky/P-MOSFET]────────────────────────┐    ║
║                                                             │    ║
║                                              [INA226 #5 @ 0x44] ║
║                                              R_shunt = 100 mΩ   ║
║                                              měření: vstupní    ║
║                                              napětí + proud     ║
║                                              nabíječe           ║
║                                                             │    ║
║                                              [CN3722 nabíječ]   ║
║                                              EN pull-down 100kΩ ║
║                                                             │    ║
║                                         [Baterie JST-PH-2]      ║
║                                         4S LiIon 11.3–16.8 V   ║
╚══════════════════════════════════════════════════════════════════╝
               │
         [HLAVNÍ VYPÍNAČ]  ← SPST, min 20V/2A (např. rocker nebo toggle)
               │            Nabíjení VYŽADUJE zapnutý spínač (pull-down na EN)
               │
         [Polyfuse 2 A]   ← ochrana před zkratem
               │
      ┌────────┤
      │   [INA226 #1 @ 0x40]  R_shunt = 10 mΩ
      │    měření: celkové napětí baterie + celkový proudový odběr
      │        │
      │   ┌────┴──────────────────────────────────────────┐
      │   │                   │                           │
      │ [AP63300 → 10 V / 3 A]             [AP63300 → 5 V / 3 A]
      │   Motor supply                      LED + Beeper supply
      │   (stabilní i při 11.3 V batt.)         │
      │        │                           [INA226 #3 @ 0x42]
      │  [INA226 #2 @ 0x41]  R_shunt = 50 mΩ    R_shunt = 100 mΩ
      │   měření: napětí + proud motoru         │
      │        │                      ┌─────────┴────────────┐
      │   [DRV8874]                40× SK6812          [Beeper 2N7000]
      │   AIPROPI → pin 36         NeoPixel              z 5 V
      │        │                   LEDs
      │   [JGY-370 motor]
      │
      └── [AP63300 → 3.3 V / 1 A]
          Logic supply
               │
          [INA226 #4 @ 0x43]  R_shunt = 200 mΩ
           měření: napětí + proud logiky
               │
     ┌─────────┼───────────────────────────────┐
   ESP32     OLED       RF přij.    POT    TCRT5000
   SH1106    INA226     2× kanál   2×10kΩ  NTC
   čipy
```

### Napěťové větve — přehled

| # | INA226 adresa | Větev | Napětí | I_max | R_shunt | Napájené komponenty |
|---|---------------|-------|--------|-------|---------|---------------------|
| 1 | **0x40** | Baterie (vstup) | 11.3–16.8 V | ~5 A | 10 mΩ / 2W | celý systém |
| 2 | **0x41** | Motor | 10 V | 3 A | 50 mΩ / 1W | DRV8874 + JGY-370 |
| 3 | **0x42** | 5V | 5 V | 3 A | 100 mΩ / 0.5W | NeoPixel LED (40×) + Beeper |
| 4 | **0x43** | 3.3V (logika) | 3.3 V | 1 A | 200 mΩ / 0.25W | ESP32, OLED, INA čipy, RF, POT, NTC, TCRT |
| 5 | **0x44** | Charger input | 14–24 V | ~1 A | 100 mΩ / 0.25W | CN3722 vstup (OR-ing výstup) |

> **Pozor:** Shunty musí mít dostatečný příkon. Např. 10 mΩ při 3 A = 0.09 W → stačí 0402.
> Pro jistotu použij 2512 pouzdro na bateriové a motorové větvi.
>
> **INA #5 (charger input):** nahrazuje jednoduchý voltage divider CHARGE_DETECT na pin 34. Firmware ví přesné vstupní napětí → rozliší "17V OK" vs "12V špatný adaptér" vs "nic".

---

## 2. INA226 — adresování (A0, A1 piny)

Každý INA226 má dva adresní piny **A0** a **A1**. Každý pin může být připojen k: GND, VS (VCC), SDA, SCL → **16 unikátních adres**.

| Adresa | A1 | A0 | Použití v projektu |
|--------|----|----|--------------------|
| **0x40** | GND | GND | INA226 #1 — Baterie |
| **0x41** | GND | VS  | INA226 #2 — Motor 10V |
| **0x42** | GND | SDA | INA226 #3 — 5V (LED + beeper) |
| **0x43** | GND | SCL | INA226 #4 — 3.3V logika |
| **0x44** | VS  | GND | INA226 #5 — Charger input (17–20V) |
| 0x3C | — | — | OLED SH1106 (jiný čip) |

> **Doporučení zapojení A0/A1:**
> - GND: připoj přímo na GND
> - VS: připoj přímo na VCC (3.3V pin INA226)
> - SDA/SCL: připoj přímo na I2C linky
> Žádné pull-up/down rezistory nejsou potřeba — INA226 je detekuje vnitřně.

### Shunt rezistory — výpočet rozsahu

INA226 má full-scale range ±81.92 mV přes shunt. `I_max = 81.92 mV / R_shunt`

| Větev | R_shunt | I_max (full scale) | Rozlišení (1 LSB) |
|-------|---------|-------------------|-------------------|
| BAT 0x40 | 10 mΩ | 8.2 A | 2.5 mA |
| Motor 0x41 | 50 mΩ | 1.6 A | 0.5 mA |
| 5V 0x42 | 100 mΩ | 0.82 A | 0.25 mA |
| Charger in 0x44 | 100 mΩ | 0.82 A | 0.25 mA |
| 3.3V 0x43 | 200 mΩ | 0.41 A | 0.125 mA |

> Kalibrační registr INA226 nastav přes `ina.setCalibration_32V_2A()` nebo vlastní
> hodnotou dle datasheetu (reg 0x05 = 5120 / R_shunt_mΩ × Calibration_coeff).

---

## 3. Motor — regulovaný zdroj 10 V

### Proč 10 V (ne přímo z baterie)

| Přístup | Výhoda | Nevýhoda |
|---------|--------|----------|
| Přímo z baterie (11.3–16.8V) | Jednoduché | Otáčky motoru se mění s napětím bat. (48% rozdíl), při 16.8V motor přetížen |
| **Regulovaných 10 V (buck)** | Stabilní otáčky, motor vždy v spec | Přidání jednoho AP63300 |

JGY-370 je jmenovitě 12V, ale pracuje správně od ~8V do 15V. **10V** je dobrý kompromis:
- Stabilní otáčky → konzistentní dávkování granulí
- Motor není přetížen ani při plné baterii (16.8V)
- Buck z 11.3V na 10V = jen 1.3V dropout → AP63300 zvládne (min dropout ~0.3V)

### AP63300 — nastavení výstupního napětí

`V_out = 0.6 V × (1 + R1/R2)`

| V_out | R1 | R2 | Poznámka |
|-------|----|----|----------|
| 5.0 V | 66.5 kΩ | 10 kΩ | 5V větev |
| 10.0 V | 156 kΩ | 10 kΩ | Motor větev |
| 3.3 V | 45 kΩ | 10 kΩ | nebo použ. adj. resistor z DS |

Použij nejbližší hodnoty z řady E96. Vždy přidej 100 nF bypass na FB pin.

---

## 4. Nabíjecí subsystém

```
[USB-C PD]              [DC Barrel Jack 5.5/2.1 mm]
TPD63A02                ~17–20 V přímý vstup
žádá 20 V PD            │
     │                  │
     └──[Schottky dioda OR-ing (BAT54) nebo P-MOSFET]──┐
                                                         │
                                                  [INA226 #5 @ 0x44]
                                                  R_shunt = 100 mΩ
                                                  měří: U_in + I_in
                                                         │
                                                    [CN3722]
                                                    CC/CV 4S charger
                                                    vstup max 28 V
                                                    EN ← ESP32 pin 32
                                                         (pull-down 100kΩ → GND)
                                                    PROG ← R pro I_charge
                                                         │
                                                   [Baterie JST-PH-2]
```

### CN3722 — R_PROG pro nastavení proudu nabíjení

| R_PROG | I_charge | Poznámka |
|--------|----------|----------|
| 3.0 kΩ | ~320 mA (0.1C) | Pomalé, bezpečné |
| **1.5 kΩ** | **~640 mA (0.2C)** | **Doporučeno** |
| 0.75 kΩ | ~1280 mA (0.4C) | Max pro JST-PH-2 konektor |

> Konec nabíjení detekuje ESP32: `voltage >= 16.7V && current < 64 mA` → `CHARGE_FULL`

### CHARGE_DETECT — voltage divider na pin 34

Pin 34 je INPUT ONLY (max 3.3V). Voltage divider z CN3722 vstupu:

```
CN3722_IN (17-20V) ── [R1=100kΩ] ── pin34 ── [R2=10kΩ] ── GND
```

Při 17V vstupu: `V_pin34 = 17 × 10/(100+10) = 1.55V` → HIGH detekce v ESP32 ✓

---

## 5. Vstup napájení — konektory, vypínač, ochrana

### Blok vstupního napájení (detail)

```
[USB-C Type-C receptacle]        [Barrel Jack 5.5/2.1 mm]
       │                                   │
  CC1, CC2 → [TPD63A02]          přímý vstup ~17–20 V
  D+, D- (pro PD komunikaci)              │
       │                                   │
       └──────── [OR-ing] ────────────────┘
                     │
                     │  Vstupní napájení nabíječe (17–20 V)
                     │
                [CN3722]  ← CC/CV 4S charger, vstup max 28V
                  EN ──[100 kΩ]── GND  ← pull-down: default VYP
                  │  └── ESP32 pin 32    ← OUTPUT HIGH = nabíjení ZAP
                  PROG ← 1.5 kΩ (640mA)
                  CHRG ← status (volitelně na ESP GPIO)
                     │
               [JST-PH-2]  ← Baterie 4S (11.3–16.8V, max 2A)
```

> **Chování CHARGE_ENABLE (pin 32) s pull-down 100 kΩ:**
>
> | Stav ESP32 | Stav pinu 32 | CN3722 EN | Poznámka |
> |------------|--------------|-----------|---------|
> | Vypnutý (switch OFF) | floating → pull-down → GND | **VYP** | Bez NTC ochrany → nabíjení zakázáno |
> | Bootuje (setup()) | OUTPUT LOW (pin 32 je OUTPUT) | VYP — bezpečná inicializace | |
> | Běží, zdroj připojen, teplota OK | OUTPUT HIGH | **ZAP** | NTC monitorováno ✓ |
> | Běží, přehřátí / baterie plná | OUTPUT LOW | VYP | |
>
> **Klíčová vlastnost:** nabíjení funguje pouze při zapnutém vypínači → ESP32 běží → NTC teplota je monitorována → bezpečné nabíjení.

```
[JST-PH-2 baterie]
       │
  [HLAVNÍ VYPÍNAČ]   ← galvanické přerušení napájení obvodu
       │              Zařízení MUSÍ být zapnuto pro nabíjení (CN3722 EN pull-down)
  [Polyfuse 2A]      ← resettovatelná pojistka
       │
  [INA226 @ 0x40]   ← vstupní bod celého obvodu
       │
     ...
```

### Hlavní vypínač — volba

| Typ | Příklad | Poznámka |
|-----|---------|----------|
| **Rocker switch SPST** | SS-12D10G / SS-22D10G | Nejčastější, snadno montovatelný do panelu |
| Toggle switch SPST | MTS-101 | Robustnější, vojenský styl |
| Slide switch | SS-12F44G | Kompaktní, pro menší zařízení |
| Soft power (P-MOSFET + ESP32) | IRF9540 / AO3415 | Bez mechanického kontaktu, ESP může sám odepnout |

> **Minimální parametry:** DC 20V / 2A (kvůli 16.8V baterii + špičky)
> **Doporučení:** Rocker switch 2A / 250VAC — běžně dostupný, snadno montovatelný, visuálně jasné ON/OFF.
> Pokud použiješ soft power přes MOSFET, přidej přídržný obvod (latch) aby ESP stihlo nastartovat.

### USB-C PD konektor — fyzické zapojení

```
USB-C receptacle
  VBUS ──── [TVS dioda, např. PRTR5V0U2X] ──── výstup PD → TPD63A02 VBUS
  GND  ──── GND
  CC1  ──── 5.1 kΩ pull-down na GND  ← identifikace zařízení jako "sink"
  CC2  ──── 5.1 kΩ pull-down na GND
             └─── také → TPD63A02 CC1/CC2 (pro PD negociaci 20V)
  D+, D- ── nepoužito (nebo volitelně na ESD ochranu)
```

> **Pozor:** 5.1 kΩ pull-down na CC piny je **povinný** pro identifikaci jako USB-C zařízení (Sink).
> Bez pull-downů zdroj nespustí výstupní napětí.
> TPD63A02 tyto odpory obvykle obsahuje interně — ověřit v datasheetu.

### OR-ing — výběr vstupního zdroje (USB-C PD nebo barrel jack)

Oba vstupy mohou být zapojeny současně — potřebujeme OR-ing aby se nevybíjely navzájem:

**Možnost A: 2× Schottky dioda (jednoduchá)**
```
USB-C PD výstup ──[BAT85 / 1N5819]──┐
Barrel jack      ──[BAT85 / 1N5819]──┴── CN3722 VIN
```
- Výhoda: jednoduché, 2 součástky
- Nevýhoda: pokles napětí 0.3–0.5V na diodě → trochu méně efektivní

**Možnost B: P-MOSFET ideal diode (bez úbytku)**
```
USB-C PD výstup ──[P-MOSFET AO3415]──┐
Barrel jack      ──[P-MOSFET AO3415]──┴── CN3722 VIN
```
- Výhoda: téměř nulový úbytek, efektivnější
- Nevýhoda: složitější, potřeba gate driver logiky nebo IC (např. LM74610)

### Ochrana vstupu

| Ochrana | Součástka | Umístění |
|---------|-----------|----------|
| ESD / přepětí (USB-C VBUS) | PRTR5V0U2X nebo TVS 24V | Na VBUS USB-C |
| Přepólování baterie | P-MOSFET nebo Schottky | Na + pin JST-PH-2 |
| Přepólování barrel jack | Schottky (BAT54) nebo P-MOSFET | Na + vstup barrel jacku |
| Zkrat výstupu | Polyfuse 2A RUEF200 | Za vypínačem |

> **Přepólování baterie:** JST-PH-2 konektor má klíčování → přepólování je fyzicky obtížné, ale doporučeno přidat Schottky nebo P-MOSFET pro jistotu.

### Indikace napájení

```
3.3V větev ──[1 kΩ]──[LED zelená]──GND    ← svítí když je obvod zapnutý
5V větev   ──[1 kΩ]──[LED modrá ]──GND    ← svítí když je 5V OK (volitelné)
```

---

## 6. Self-test — co testuje každý INA

S 5× INA226 na každé větvi se self-test výrazně vylepší:

| Test | Větev | INA | Postup | Pass kritérium |
|------|-------|-----|--------|----------------|
| Baterie — napětí | BAT | 0x40 | getBusVoltage() | 11.3–16.8V |
| Baterie — klidový proud | BAT | 0x40 | getCurrent() při IDLE | < 500mA (bez motoru) |
| Motor — delta proud | Motor | 0x41 | baseline → 80% PWM → delta | delta > 5mA |
| Motor — napětí větve | Motor | 0x41 | getBusVoltage() | 9.5–10.5V |
| LED — delta proud | 5V | 0x42 | baseline → all white → delta | delta > 180mA |
| **Beeper — delta proud** | **5V** | **0x42** | **baseline → beep 100ms → delta** | **delta > 20mA** |
| LED + beeper napětí | 5V | 0x42 | getBusVoltage() | 4.8–5.2V |
| Logika — proud | 3.3V | 0x43 | getCurrent() při IDLE | 50–400mA |
| Logika — napětí | 3.3V | 0x43 | getBusVoltage() | 3.1–3.4V |
| **Charger vstup — napětí** | **Charger** | **0x44** | **getBusVoltage()** | **informativní — 0V=odpojen, 14–24V=OK** |

> **Beeper self-test přes INA:** 2N7000 MOSFET + pasivní bzučák na 5V větvi = ~50–150mA delta
> při zapnutí. Spolehlivě ověří jak bzučák, tak MOSFET budič.
>
> **Charger input INA:** při self-testu jen info výpis. Nepřítomnost zdroje není chyba.

---

## 6. I2C bus

- **SDA = pin 21, SCL = pin 22** (ESP32)
- Napájení sběrnice: 3.3 V
- Pull-up rezistory: **4.7 kΩ × 2** na 3.3 V (jeden na SDA, jeden na SCL)
- Maximální délka trasy na DPS: ~30 cm bez repeateru

| Adresa | Čip | Větev | Knihovna |
|--------|-----|-------|----------|
| 0x40 | INA226 | BAT — baterie | Adafruit_INA226 |
| 0x41 | INA226 | Motor 10V větev | Adafruit_INA226 |
| 0x42 | INA226 | 5V (LED + beeper) | Adafruit_INA226 |
| 0x43 | INA226 | 3.3V logika | Adafruit_INA226 |
| 0x44 | INA226 | Charger input (17–20V) | Adafruit_INA226 |
| 0x3C | SH1106 OLED | 3.3V | U8g2 |

> Výhoda 5× stejný čip: jedno pouzdro, jedna knihovna, jedna řada adres.
> Všechny INA226 jsou identické — snížení počtu různých komponent v BOM.
> INA #5 @ 0x44 nahrazuje voltage divider + pin 34 (CHARGE_DETECT) — pin 34 uvolněn.

---

## 7. ESP32 — kompletní pinout

| Pin | Funkce | Typ | Napájení periferie | Poznámka |
|-----|--------|-----|--------------------|----------|
| 1 | TX (UART0) | — | — | Nepoužívat jako GPIO |
| 3 | RX (UART0) | — | — | Nepoužívat jako GPIO |
| 4 | FLOW_PIN (TCRT5000 D0) | INPUT_PULLUP / ISR | 3.3 V | FALLING ISR |
| 5 | BEEPER (PWM ch2) | OUTPUT | 5 V přes 2N7000 | 2 kHz, 50% duty |
| 13 | BTN — RF přijímač ch1 | INPUT_PULLUP | 3.3 V | BTN_INVERT=1 |
| 14 | BTN2 — RF přijímač ch2 | INPUT_PULLUP | 3.3 V | BTN2_INVERT=1 |
| 16 | RPWM → DRV8874 IN1 | OUTPUT/PWM ch0 | logika 3.3 V | 20 kHz |
| 17 | R_EN (nSLEEP DRV8874) | OUTPUT | logika 3.3 V | HIGH = motor aktiv. |
| 18 | LPWM → DRV8874 IN2 | OUTPUT/PWM ch1 | logika 3.3 V | 20 kHz |
| 19 | L_EN (rezerva/FAULT) | OUTPUT/INPUT | logika 3.3 V | volitelně FAULT pin |
| 21 | I2C SDA | I2C | 3.3 V | pull-up 4.7 kΩ |
| 22 | I2C SCL | I2C | 3.3 V | pull-up 4.7 kΩ |
| 23 | DRV8874 nSLEEP / rezerva | OUTPUT | logika 3.3 V | volný GPIO |
| 25 | POT_SPEED (ADC) | INPUT ADC | 3.3 V dělič | 10 kΩ potenciometr |
| 26 | POT_BEEP (ADC) | INPUT ADC | 3.3 V dělič | 10 kΩ potenciometr |
| 27 | NEO_PIN (NeoPixel data) | OUTPUT | 5 V / level shift | ⚠ SK6812 nebo shifter |
| 32 | CHARGE_ENABLE | OUTPUT | → CN3722 EN | |
| 33 | COVER (snímač krytu) | INPUT_PULLUP | 3.3 V | neosazeno (USE_COVER=0) |
| 34 | CHARGE_DETECT *(opt.)* | **INPUT ONLY** | voltage divider | Záložní detekce bez USE_INA_CHARGER; s INA #5 nevyužito |
| 35 | NTC_PIN (ADC) | **INPUT ONLY** ADC | 3.3 V dělič | nelze OUTPUT! |
| 36 | AIPROPI z DRV8874 | **INPUT ONLY** ADC | analogový proud | přes R 3.3 kΩ |
| 39 | rezerva | **INPUT ONLY** ADC | — | záložní ADC |

**Bootstrap piny — nepoužívat pro vstupy s externím pull:**
`0, 2, 12, 15` — mohou ovlivnit boot mode.

---

## 8. H-bridge — DRV8874

Doporučená náhrada za BTS7960B (43A automotive, overkill pro JGY-370 ~1.5A stall):

| Parametr | BTS7960B | **DRV8874** |
|----------|----------|-------------|
| Max proud | 43 A | 3.5 A |
| Napájení VM | 4.5–44 V | 4.5–37 V |
| Proudový výstup | ne | **AIPROPI** (analog) |
| Pouzdro | TO-220 (2 čipy) | HTSSOP-14 (1 čip) |

### DRV8874 — AIPROPI výpočet

`I_AIPROPI = I_motor / 1306` (current mirror ratio)

Přes R_sense = 3.3 kΩ → `V_pin36 = I_motor × 3.3kΩ / 1306`

| I_motor | V_pin36 |
|---------|---------|
| 100 mA | 0.25 V |
| 500 mA | 1.26 V |
| 1000 mA | 2.53 V |
| 1500 mA (stall) | 3.79 V → ořízne ADC, použij R=2.2 kΩ |

> Pro stall detekci do 1.5A: použij R_sense = **2.2 kΩ** → max 2.53V při 1.5A ✓

---

## 9. NeoPixel LEDs

| Varianta | Napájení | Data VIH | Doporučení |
|----------|----------|----------|------------|
| **SK6812** | 3.3–5 V | **3.3 V OK** | **Doporučeno** |
| WS2815 | 12 V | 3.3 V OK | Přímé bat. napájení, záložní data |
| WS2812B | 3.5–5.3 V | min 3.5 V ⚠ | Nespolehlivé při 3.3V ESP32 |

Level shifter (pokud WS2812B): **74AHCT125**, OE na GND.

### Výkon (max při bílé, full jas)

`40 LED × 60 mA = 2400 mA` → nutný limit jasu v kódu nebo max 30 mA/LED

---

## 10. Beeper — MOSFET budič z 5V

```
ESP32 pin 5 ──[10 kΩ]── Gate 2N7000
                              │ Drain ── bzučák (−)
                              │ Source ── GND
             GND ──[100 kΩ]── Gate    ← pull-down při startu
                         bzučák (+) ── 5 V
```

- Frekvence: 2000 Hz (KY-006), jiné zkus 1000–4000 Hz
- Duty cycle: 50% (128/255) = max hlasitost
- Pod 7% hlasitost → beeper se vůbec nespustí (tichý režim)
- Self-test: INA226 @ 0x42 změří proud delta při spuštění tónu

---

## 11. NTC termistor (USE_NTC)

```
3.3 V ── [10 kΩ] ── pin 35 ── [10 kΩ NTC @ 25°C] ── GND
```

Beta koeficient: 3950, umístění: na bateriový pack.
Stop nabíjení při > 45 °C, resume při < 40 °C.

---

## 12. TCRT5000 — průtok granulí (USE_FLOW)

```
VCC ── 3.3 V  |  GND  |  D0 ── pin 4 (INPUT_PULLUP, FALLING ISR)
Trimmer: D0 = HIGH bez překážky, D0 = LOW = granule v paprsku
```

Timeout 3 s bez pulzu při chodu motoru → `errorMsg = "Hopper empty"`. Auto-reset při zastavení.

---

## 13. Checklist komponent pro PCB

### Napájení — vstup a distribuce
- [ ] **Hlavní vypínač SPST** min 20V/2A (rocker switch nebo toggle)
- [ ] **USB-C Type-C receptacle** (GCT USB4135 nebo HRO TYPE-C-31-M-12)
- [ ] **TPD63A02** — USB-C PD kontrolér (žádá 20V PD)
- [ ] 2× **5.1 kΩ** pull-down na CC1, CC2 (pokud TPD63A02 nemá interní)
- [ ] **TVS dioda** na VBUS USB-C (PRTR5V0U2X nebo 24V TVS)
- [ ] **DC Barrel Jack 5.5/2.1 mm** (PJ-002A nebo ekvivalent)
- [ ] **OR-ing**: 2× Schottky BAT85 nebo 2× P-MOSFET AO3415
- [ ] **CN3722** (MSOP-10) + R_PROG 1.5 kΩ + **100 kΩ pull-down na EN pin** (→ GND)
- [ ] **JST-PH-2 konektor** (baterie) + volitelně ochranná Schottky/P-MOSFET
- [ ] **Polyfuse 2 A** (RUEF200) — za vypínačem
- [ ] **5× INA226** (SOT-23-8 nebo MSOP-8) — všechny stejné!
- [ ] 5× R_shunt: 10 mΩ / 2W (2512), 50 mΩ / 1W (2512), 100 mΩ (1206) ×2, 200 mΩ (1206)
- [ ] *(optional)* Voltage divider CHARGE_DETECT: 100 kΩ + 10 kΩ na pin 34 — záloha bez INA #5
- [ ] AP63300 #1 → **10 V** / 3 A — Motor supply
- [ ] AP63300 #2 → **5 V** / 3 A — LED + Beeper
- [ ] AP63300 #3 → **3.3 V** / 1 A — Logika
- [ ] 3× induktor 4.7 µH (Bourns SRR1260 nebo ekvivalent, 3A rated)
- [ ] Bulk cap: 220 µF / 16V (5V větev), 100 µF / 16V (3.3V), 220 µF / 25V (10V, mot.)
- [ ] Decoupling 100 nF u každého IC (0402/0603)

### Nabíjení
- [ ] CN3722 (MSOP-10) + R_PROG 1.5 kΩ
- [ ] USB-C PD kontrolér TPD63A02
- [ ] DC barrel jack 5.5/2.1 mm
- [ ] Schottky dioda BAT54 / BAT85 pro OR-ing vstupů (nebo P-MOSFET)
- [ ] Voltage divider CHARGE_DETECT: 100 kΩ + 10 kΩ

### H-bridge / Motor
- [ ] DRV8874 (HTSSOP-14) + bootstrap cap 100 nF
- [ ] R_AIPROPI = 2.2 kΩ (pro pin 36)
- [ ] Konektor motoru 2pin min 3 A

### LED / Display
- [ ] 40× SK6812 (nebo WS2812B + 74AHCT125 level shifter)
- [ ] SH1106 OLED modul

### Vstupy / Senzory
- [ ] 2× Potenciometr 10 kΩ B (lineární)
- [ ] RF přijímač 2-kanálový
- [ ] TCRT5000 modul
- [ ] NTC 10 kΩ @ 25°C + sériový R 10 kΩ
- [ ] Snímač krytu (volitelné)

### Ostatní
- [ ] ESP32 WROOM-32
- [ ] Pasivní bzučák 2 kHz + 2N7000 + 10 kΩ / 100 kΩ
- [ ] Reset tlačítko (EN pin)
- [ ] Boot tlačítko (pin 0 → GND přes 10 kΩ + tlačítko)
- [ ] LED indikace 3.3V — zelená + 1 kΩ (obvod zapnut)
- [ ] LED indikace 5V — modrá + 1 kΩ (volitelné)
- [ ] UART header (TX/RX/GND) nebo Tag-Connect
- [ ] I2C pull-up 4.7 kΩ × 2 (SDA, SCL)
- [ ] Ferrite bead na 3.3V ADC větev (omezení PWM šumu)

---

## 14. Firmware — implikace pro kód

Přechod na 4× INA226 vyžaduje:

```cpp
// Nahradit Adafruit_INA219 za Adafruit_INA226 (podobné API)
#include <Adafruit_INA226.h>

Adafruit_INA226 ina_bat(0x40);   // baterie — napětí + celkový proud
Adafruit_INA226 ina_mot(0x41);   // motor 10V větev
Adafruit_INA226 ina_5v (0x42);   // 5V: LED + beeper
Adafruit_INA226 ina_3v3(0x43);   // 3.3V logika
Adafruit_INA226 ina_chg(0x44);   // charger input: přesné U_in → nahrazuje CHARGE_DETECT pin 34

// Kalibrační hodnoty (nastavit dle R_shunt):
// ina_bat.setCalibration_32V_2A() — nebo vlastní dle 10mΩ
// ina_mot.setCalibration_16V_400mA() — nebo vlastní dle 50mΩ
```

### Watchdog timer

```cpp
#include "esp_task_wdt.h"
// V setup() — po runSelfTest():
esp_task_wdt_init(WDT_TIMEOUT_S, true);  // reset při zamrznutí loop >10s
esp_task_wdt_add(NULL);
// V loop():
esp_task_wdt_reset();  // krmit na konci každé iterace
// OTA onStart callback:
esp_task_wdt_delete(NULL);  // OTA trvá déle — dočasně vypnout
```

Nové možnosti self-testu:
- Beeper delta proud přes INA 0x42
- Každá větev: ověření napětí i proudu
- Diagnostika na display: zobrazit proudy všech větví

---

## 15. Časté chyby — co neopomenout

| # | Problém | Řešení |
|---|---------|--------|
| 1 | Pin 27 (NEO_PIN) = 3.3V, WS2812B chce min 3.5V | SK6812 nebo 74AHCT125 |
| 2 | Piny 34, 35, 36, 39 — INPUT ONLY | Nelze OUTPUT |
| 3 | INA226 A0/A1 připojeny na SDA/SCL — krátká inicializace I2C | Inicializuj `Wire.begin()` PŘED `ina.begin()` |
| 4 | Motor **nesmí** jít přes JST-PH-2 konektor (max 2A) | Motor má vlastní konektor na DRV8874 |
| 5 | Bootstrap piny 0, 2, 12, 15 | Nezapojovat pull při startu |
| 6 | UART piny 1 (TX), 3 (RX) | Nepoužívat jako GPIO |
| 7 | ESP32 ADC + aktivní WiFi (USE_OTA) | ADC1 se chová divně při WiFi — používej INA hodnoty |
| 8 | PWM kanály sdílí timer | Motor ch0+ch1 @ 20kHz, Beeper ch2 @ 2kHz — různé timery ✓ |
| 9 | CN3722 EN — pull-down 100kΩ na GND | FW: HIGH = nabíjení ZAP; device OFF → VYP → NTC bezpečnost |
| 10 | I2C bez pull-up | 4.7 kΩ na SDA i SCL na 3.3V |
| 11 | Motor buck při 11.3V batt → 10V output: jen 1.3V headroom | AP63300 zvládne (min dropout ~0.3V), ale přidej 100µF vstupní kondenzátor |
| 12 | Shunt rezistor — příkon | P = I² × R, pro 3A přes 50mΩ = 450mW → 2512 pouzdro (500mW rated) |

---

## 16. Doporučené kroky návrhu PCB

1. **Schéma** — začni napájecí sekcí (bat → polyfuse → 4× INA226 větve), pak MCU
2. **Footprinty** — ověř pouzdra AP63300, DRV8874, INA226 (SOT-23-8), CN3722 před objednávkou
3. **Rozložení** — silové trasy (10V, 5V, BAT) co nejdál od ADC a I2C signálů
4. **Tloušťky tras:**
   - BAT/Motor: ≥ 2 mm (pro 3A)
   - 5V LED: ≥ 1.5 mm
   - 3.3V logika: ≥ 0.5 mm
   - Signálové / I2C: 0.2–0.25 mm
5. **Via stitch GND** — pod ESP32 modulem (teplo + RF ground plane)
6. **Hvězdicová GND** — analog GND (ADC, NTC) oddělen od silové GND (motor, LED), spojeny v jednom bodě
7. **Testovací body** — na každém napájení (5V, 3.3V, 10V, BAT), I2C SDA/SCL, AIPROPI, CHARGE_DETECT
8. **DRC** → výroba (JLCPCB/PCBWay, min trace 0.1mm, min via 0.2mm drill)

---

*Poslední aktualizace: 2026-02-19 | Firmware: FW/petfw_na_baterii/petfw_na_baterii.ino*
