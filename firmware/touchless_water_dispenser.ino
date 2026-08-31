/*
 * ============================================================================
 *  Automatic Touchless Water Dispenser
 * ----------------------------------------------------------------------------
 *  Description:
 *    A hands-free water dispensing system built on an Arduino Uno. An HC-SR04
 *    ultrasonic sensor continuously measures the distance to whatever is in
 *    front of it. When an object (a hand, cup, or bottle) is detected inside
 *    a defined activation range, the Arduino energizes a relay that switches
 *    a DC water pump ON. When the object is removed, the pump switches OFF.
 *
 *  Hardware:
 *    - Arduino Uno (ATmega328P)
 *    - HC-SR04 Ultrasonic Distance Sensor
 *    - 1-Channel 5V Relay Module (driving a DC submersible water pump)
 *    - Status LED (optional, on-board or external)
 *
 *  Pin Map:
 *    TRIG_PIN   -> D9   (Arduino output  -> HC-SR04 TRIG)
 *    ECHO_PIN   -> D10  (HC-SR04 ECHO    -> Arduino input)
 *    RELAY_PIN  -> D7   (Arduino output  -> Relay IN)
 *    STATUS_LED -> D13  (Arduino output  -> indicator LED)
 *
 *  Safety / design notes:
 *    - The HC-SR04 ECHO line is 5V logic. If the relay module or pump draws
 *      current on the same 5V rail as a 3.3V-logic board, a voltage divider
 *      would be needed on ECHO. On the Arduino Uno (5V logic) this is NOT
 *      required.
 *    - A maximum run-time cutoff (MAX_DISPENSE_TIME_MS) is included so the
 *      pump cannot be left running indefinitely if an object is left in
 *      front of the sensor (e.g., a cup placed and forgotten, or a sensor
 *      fault). This is a basic safety/water-conservation feature that most
 *      hobbyist versions of this project skip.
 *    - Simple software debouncing (CONFIRM_READINGS) avoids false triggers
 *      from a single noisy ultrasonic reading.
 *
 *  Author: Adu-Gyamfi Kwadwo
 *  Course/Project: Third-Year Group Project - Electrical/Electronic Engineering
 * ============================================================================
 */

// ----------------------------- Pin definitions -----------------------------
const uint8_t TRIG_PIN    = 9;
const uint8_t ECHO_PIN    = 10;
const uint8_t RELAY_PIN   = 7;
const uint8_t STATUS_LED  = 13;

// ----------------------------- Tunable parameters ---------------------------
const float    ACTIVATION_DISTANCE_CM = 10.0;   // trigger if object is closer than this
const float    MIN_VALID_DISTANCE_CM  = 2.0;    // ignore readings below sensor's reliable range
const uint8_t  CONFIRM_READINGS       = 3;       // consecutive detections needed before turning pump ON
const uint16_t SAMPLE_INTERVAL_MS     = 60;      // time between ultrasonic pings
const uint32_t MAX_DISPENSE_TIME_MS   = 8000UL;  // safety cutoff: max continuous pump run-time
const uint32_t COOLDOWN_AFTER_CUTOFF_MS = 1500UL;// forced pause after a safety cutoff

// Set to true if the relay module is "active LOW" (most cheap 1-channel
// relay boards energize when IN is pulled LOW). Set to false for
// "active HIGH" boards. Check your specific module's datasheet/silkscreen.
const bool RELAY_ACTIVE_LOW = true;

// ----------------------------- State variables ------------------------------
uint8_t  consecutiveDetections = 0;
bool     pumpIsOn              = false;
uint32_t pumpStartTime         = 0;
bool     inCooldown            = false;
uint32_t cooldownStartTime     = 0;
uint32_t lastSampleTime        = 0;

// =============================================================================
void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);

  setPump(false); // ensure pump starts OFF
  digitalWrite(STATUS_LED, LOW);

  Serial.println(F("Touchless Water Dispenser - system initialized."));
}

// =============================================================================
void loop() {
  uint32_t now = millis();

  // Handle a forced cooldown period after a safety cutoff
  if (inCooldown) {
    if (now - cooldownStartTime >= COOLDOWN_AFTER_CUTOFF_MS) {
      inCooldown = false;
      consecutiveDetections = 0;
    } else {
      return; // skip sensing/dispensing while cooling down
    }
  }

  // Take a distance reading no more often than SAMPLE_INTERVAL_MS
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;
    float distance = readDistanceCM();
    handleDetection(distance, now);
  }

  // Enforce the maximum continuous run-time safety cutoff
  if (pumpIsOn && (now - pumpStartTime >= MAX_DISPENSE_TIME_MS)) {
    Serial.println(F("Safety cutoff: max dispense time reached. Stopping pump."));
    setPump(false);
    inCooldown = true;
    cooldownStartTime = now;
  }
}

// =============================================================================
// Sends a 10us pulse on TRIG and measures the ECHO pulse width to compute
// distance in centimeters. Returns -1.0 if no valid echo was received
// (i.e., nothing in range, or a sensor timeout).
float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 25ms timeout ≈ ~4.3 m maximum range; prevents pulseIn() from blocking forever
  uint32_t durationUs = pulseIn(ECHO_PIN, HIGH, 25000UL);

  if (durationUs == 0) {
    return -1.0; // no echo received (out of range / timeout)
  }

  // Speed of sound ≈ 343 m/s at room temperature -> 0.0343 cm/us.
  // Divide by 2 because the pulse travels to the object and back.
  float distanceCm = (durationUs * 0.0343f) / 2.0f;
  return distanceCm;
}

// =============================================================================
// Applies the debounced activation logic: an object must be detected within
// range for CONFIRM_READINGS consecutive samples before the pump turns on,
// and the pump turns off as soon as a single "no object" reading occurs.
void handleDetection(float distanceCm, uint32_t now) {
  bool objectPresent = (distanceCm > MIN_VALID_DISTANCE_CM) &&
                        (distanceCm <= ACTIVATION_DISTANCE_CM);

  if (objectPresent) {
    consecutiveDetections++;
    Serial.print(F("Distance: "));
    Serial.print(distanceCm);
    Serial.println(F(" cm  [object in range]"));

    if (consecutiveDetections >= CONFIRM_READINGS && !pumpIsOn) {
      setPump(true);
      pumpStartTime = now;
    }
  } else {
    consecutiveDetections = 0;
    if (pumpIsOn) {
      setPump(false);
    }
  }
}

// =============================================================================
// Turns the pump (relay) and status LED on/off, honoring the relay's
// active-HIGH / active-LOW wiring.
void setPump(bool turnOn) {
  pumpIsOn = turnOn;
  bool relaySignal = RELAY_ACTIVE_LOW ? !turnOn : turnOn;
  digitalWrite(RELAY_PIN, relaySignal ? HIGH : LOW);
  digitalWrite(STATUS_LED, turnOn ? HIGH : LOW);

  Serial.println(turnOn ? F(">>> Pump ON") : F(">>> Pump OFF"));
}
