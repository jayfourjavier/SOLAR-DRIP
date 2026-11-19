#include <Arduino.h>
/* 
* WIRING:
* SOLAR PANEL: POSITIVE (RED) -> SOLAR CHARGE CONTROLLER PV+
* SOLAR PANEL: NEGATIVE (BLACK) -> SOLAR CHARGE CONTROLLER PV-
* 12V BATTERY: POSITIVE (YELLOW) -> TOGGLE SWITCH -> SOLAR CHARGE CONTROLLER BAT+
* 12V BATTERY: NEGATIVE (BLUE) -> SOLAR CHARGE CONTROLLER BAT-
* SOLAR CHARGE CONTROLLER LOAD+ -> TOGGLE SWITCH -> ARDUINO UNO (VIN) (YELLOW) + CYTRON MD10C MOTOR DRIVER (VIN) (YELLOW)
* SOLAR CHARGE CONTROLLER LOAD- -> ARDUINO UNO (GND) (BLUE) + CYTRON MD10C MOTOR DRIVER (GND) (BLUE)
* CYTRON MD10C MOTOR DRIVER: DIR (BLUE) -> ARDUINO UNO (D10)
* CYTRON MD10C MOTOR DRIVER: PWM (YELLOW) -> ARDUINO UNO (D11)
* ANALOG CAPACITIVE SOIL MOISTURE SENSOR: VCC (BLUE) -> ARDUINO UNO (5V)
* ANALOG CAPACITIVE SOIL MOISTURE SENSOR: GND (BLACK) -> ARDUINO UNO (GND)
* ANALOG CAPACITIVE SOIL MOISTURE SENSOR: AOUT (YELLOW) -> ARDUINO UNO (A0)
*/


/* Time unit helpers: convert to milliseconds. Use inside the settings below.
 * Examples: SECONDS(10), MINUTES(30), MINUTES(1) + SECONDS(30)
 */
#define SECONDS(x)  ((unsigned long)(x) * 1000UL)
#define MINUTES(x)  ((unsigned long)(x) * 60000UL)

/* ========================= USER SETTINGS =========================
 * Edit the values below to tune the system. No other code changes needed.
 */

// PUMP SPEED: 0-100 (% of full speed). 0 = off, 100 = full speed.
#define PUMP_SPEED_PERCENT        60

// PUMPING DURATION: how long the pump runs per watering cycle.
// Use SECONDS(n) or MINUTES(n).
#define PUMP_DURATION             SECONDS(3)

// MOISTURE CHECK INTERVAL: how often the soil is checked.
// Also acts as the minimum wait between watering cycles.
// Use SECONDS(n) or MINUTES(n).
#define MOISTURE_CHECK_INTERVAL   MINUTES(30)

// PUMP DIRECTION: 1 or 0. Swap if the pump runs backwards.
#define PUMP_DIRECTION            HIGH

// SOIL ADC THRESHOLDS (raw ADC 0-1023). Capacitive sensors read HIGHER when DRY.
// Calibrate: note the reading in air (dry) and in water (wet), then pick values
// in between. SOIL_WET_ADC_RAW must be LOWER than SOIL_DRY_ADC_RAW.
//   reading >= SOIL_DRY_ADC_RAW : soil is DRY -> watering starts
//   reading <= SOIL_WET_ADC_RAW : soil is WET -> watering stops
//   in between                  : no change (prevents rapid on/off)
#define SOIL_DRY_ADC_RAW          550
#define SOIL_WET_ADC_RAW          450

// SOIL SETTLE TIME: wait after each pump run for water to soak in before
// re-checking moisture. If still not wet, the pump runs again.
// Use SECONDS(n) or MINUTES(n).
#define SOIL_SETTLE_TIME          SECONDS(30)

// MAX PUMP CYCLES per watering session (safety against an empty tank or
// faulty sensor running the pump forever).
#define MAX_PUMP_CYCLES           5

// Set to 1 to print readings over Serial (9600 baud), 0 to disable.
#define DEBUG_SERIAL              1

/* ======================= END USER SETTINGS ======================= */

// Pin definitions
#define PIN_PUMP_DIR   10
#define PIN_PUMP_PWM   11
#define PIN_SOIL       A0

// Derived values (do not edit)
#define PUMP_PWM_VALUE            ((uint8_t)((PUMP_SPEED_PERCENT * 255UL) / 100UL))
#define PUMP_DURATION_MS          (PUMP_DURATION)
#define MOISTURE_CHECK_INTERVAL_MS (MOISTURE_CHECK_INTERVAL)
#define SOIL_SETTLE_TIME_MS       (SOIL_SETTLE_TIME)
#define SOIL_SAMPLES              8

#if SOIL_WET_ADC_RAW >= SOIL_DRY_ADC_RAW
  #error "SOIL_WET_ADC_RAW must be lower than SOIL_DRY_ADC_RAW"
#endif

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x)   Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

enum PumpState { PUMP_IDLE, PUMP_RUNNING, PUMP_SETTLING };

static PumpState pumpState = PUMP_IDLE;
static unsigned long lastCheckMs = 0;
static unsigned long pumpStartMs = 0;
static unsigned long settleStartMs = 0;
static uint8_t pumpCycles = 0;

static int readSoilMoisture() {
  unsigned long sum = 0;
  for (uint8_t i = 0; i < SOIL_SAMPLES; i++) {
    sum += analogRead(PIN_SOIL);
  }
  return (int)(sum / SOIL_SAMPLES);
}

static void pumpStart() {
  digitalWrite(PIN_PUMP_DIR, PUMP_DIRECTION);
  analogWrite(PIN_PUMP_PWM, PUMP_PWM_VALUE);
  pumpStartMs = millis();
  pumpState = PUMP_RUNNING;
  pumpCycles++;
  DEBUG_PRINT(F("Pump ON, cycle "));
  DEBUG_PRINTLN(pumpCycles);
}

static void pumpStop() {
  analogWrite(PIN_PUMP_PWM, 0);
  settleStartMs = millis();
  pumpState = PUMP_SETTLING;
  DEBUG_PRINTLN(F("Pump OFF, waiting for soil to settle"));
}

void setup() {
  pinMode(PIN_PUMP_DIR, OUTPUT);
  pinMode(PIN_PUMP_PWM, OUTPUT);
  pinMode(PIN_SOIL, INPUT);
  digitalWrite(PIN_PUMP_DIR, PUMP_DIRECTION);
  analogWrite(PIN_PUMP_PWM, 0);

#if DEBUG_SERIAL
  Serial.begin(9600);
#endif

  // Run first moisture check shortly after boot instead of waiting a full interval.
  lastCheckMs = millis() - MOISTURE_CHECK_INTERVAL_MS;
}

void loop() {
  const unsigned long now = millis();

  switch (pumpState) {
    case PUMP_IDLE:
      if (now - lastCheckMs >= MOISTURE_CHECK_INTERVAL_MS) {
        lastCheckMs = now;
        const int moisture = readSoilMoisture();
        DEBUG_PRINT(F("Soil raw: "));
        DEBUG_PRINTLN(moisture);
        if (moisture >= SOIL_DRY_ADC_RAW) {
          pumpCycles = 0;
          pumpStart();
        }
      }
      break;

    case PUMP_RUNNING:
      if (now - pumpStartMs >= PUMP_DURATION_MS) {
        pumpStop();
      }
      break;

    case PUMP_SETTLING:
      if (now - settleStartMs >= SOIL_SETTLE_TIME_MS) {
        const int moisture = readSoilMoisture();
        DEBUG_PRINT(F("Soil raw after pumping: "));
        DEBUG_PRINTLN(moisture);
        if (moisture <= SOIL_WET_ADC_RAW) {
          DEBUG_PRINTLN(F("Soil is WET, watering done"));
          lastCheckMs = now;
          pumpState = PUMP_IDLE;
        } else if (pumpCycles >= MAX_PUMP_CYCLES) {
          DEBUG_PRINTLN(F("Max pump cycles reached, giving up until next check"));
          lastCheckMs = now;
          pumpState = PUMP_IDLE;
        } else {
          pumpStart();
        }
      }
      break;
  }
}
