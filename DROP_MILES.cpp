/*
  RP2040_MILES_multi_protocol.ino
  Multi-protocol MILES transmitter example for RP2040 (Arduino).
  - Multiple protocol selection
  - BLUFOR / OPFOR switching
  - Persistent settings (EEPROM, fallback)
  - Simple UI: Next protocol, Toggle side, Fire
  - PLACEHOLDER transmitter (digitalWrite + delayMicroseconds)
    Replace laser_transmit_frame(...) with your DMA/PWM implementation for production.
*/

#include <Arduino.h>
#include <EEPROM.h>
#include "MILES_CODES_H.h"

// -------------------- Hardware pin config --------------------
const uint8_t PIN_OUT = 8;        // IR TTL driver gate (replace with your actual pin)
const uint8_t PIN_BTN_NEXT = 2;   // cycle protocols
const uint8_t PIN_BTN_SIDE = 3;   // toggle BLU/OPFOR
const uint8_t PIN_BTN_FIRE = 4;   // trigger (test button)
const uint8_t PIN_LED_BLU = 12;   // show BLUFOR
const uint8_t PIN_LED_OPF = 13;   // show OPFOR

// Timing (adjust to match MILES bin duration & pulse width; these are demo values)
const uint32_t BIN_US = 500;      // microseconds per time-slot (bin). Replace to match real MILES timing.
const uint32_t PULSE_US = 250;    // pulse width for a '1' bit inside a bin (must be <= BIN_US)

// -------------------- Persistence addresses --------------------
const int EEPROM_ADDR_MAGIC = 0;
const int EEPROM_ADDR_PROTOCOL = 4;
const int EEPROM_ADDR_SIDE = 8;
const uint32_t EEPROM_MAGIC = 0x4D494C45; // 'MILE'

// -------------------- Protocol registry --------------------
typedef struct {
  uint8_t id;
  const char *name;
  const MILES_Code *code; // pointer to 11-bit pattern for simple protocols
} ProtocolEntry;

// Reference codes from header
ProtocolEntry protocols[] = {
  { 0, "Universal Kill (Basic)", &PLAYER_UNIVERSAL_KILL },
  { 1, "Player ID 001", &PLAYER_ID_001 },
  { 2, "Player ID 002", &PLAYER_ID_002 },
  { 3, "Pause/Reset", &EVENT_PAUSE },
  { 4, "End Exercise", &EVENT_END_EXERCISE }
};

const size_t NUM_PROTOCOLS = sizeof(protocols) / sizeof(protocols[0]);

// -------------------- Active state --------------------
size_t active_index = 0;      // index into protocols[]
bool active_side_opfor = false; // false = BLUFOR, true = OPFOR

// Debounce
unsigned long last_next_press = 0;
unsigned long last_side_press = 0;
unsigned long last_fire_press = 0;
const unsigned long DEBOUNCE_MS = 200;

// EEPROM available flag
bool eeprom_ok = false;

// -------------------- Helper: persistence --------------------
void save_settings() {
  if (!eeprom_ok) {
    Serial.println("EEPROM not available; skipping save");
    return;
  }
  EEPROM.put(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  uint8_t proto_id = protocols[active_index].id;
  EEPROM.put(EEPROM_ADDR_PROTOCOL, proto_id);
  uint8_t side = active_side_opfor ? 1 : 0;
  EEPROM.put(EEPROM_ADDR_SIDE, side);
  EEPROM.commit();
  Serial.printf("Saved settings: proto=%u side=%u\n", proto_id, side);
}

void load_settings() {
  if (!eeprom_ok) {
    Serial.println("EEPROM not available; using defaults");
    return;
  }
  uint32_t magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  if (magic != EEPROM_MAGIC) {
    Serial.println("No saved settings found, using defaults");
    return;
  }
  uint8_t proto_id = 0;
  EEPROM.get(EEPROM_ADDR_PROTOCOL, proto_id);
  uint8_t side = 0;
  EEPROM.get(EEPROM_ADDR_SIDE, side);
  // Find proto_id in registry
  for (size_t i = 0; i < NUM_PROTOCOLS; ++i) {
    if (protocols[i].id == proto_id) {
      active_index = i;
      break;
    }
  }
  active_side_opfor = (side != 0);
  Serial.printf("Loaded settings: proto=%u (index=%u) side=%u\n", proto_id, (unsigned)active_index, side);
}

// -------------------- Build frame & apply side --------------------
// Expand 11-bit pattern to bit array (0/1 values)
void build_frame_from_code(const MILES_Code *c, uint8_t *out_bits, size_t *out_bits_len) {
  for (size_t i = 0; i < 11; ++i) {
    out_bits[i] = c->pattern[i] ? 1 : 0;
  }
  *out_bits_len = 11;
}

// Apply side/opfor mapping. **This is example logic** — replace with the exact bit index used by your MILES format
void apply_side_to_frame(uint8_t *bits, size_t bits_len, bool opfor) {
  // Example: choose a 'team' bit index to flip — adjust for your protocol
  const int SIDE_BIT_INDEX = 5;
  if (bits_len > (size_t)SIDE_BIT_INDEX) {
    bits[SIDE_BIT_INDEX] = opfor ? 1 : 0;
  }
}

// -------------------- Transmit (placeholder) --------------------
// This simple routine toggles PIN_OUT per bit using BIN_US and PULSE_US.
// Replace this with your DMA+PWM buffer streaming for precise timing and duty management.
void laser_transmit_frame(const uint8_t *frame_bits, size_t bitlen) {
  // Print for debug
  Serial.print("Transmit bits: ");
  for (size_t i = 0; i < bitlen; ++i) Serial.print(frame_bits[i] ? '1' : '0');
  Serial.println();

  // Important safety: ensure output low before start
  digitalWrite(PIN_OUT, LOW);
  delayMicroseconds(10);

  for (size_t i = 0; i < bitlen; ++i) {
    if (frame_bits[i]) {
      // emit pulse at start of bin for duration PULSE_US
      digitalWrite(PIN_OUT, HIGH);
      delayMicroseconds(PULSE_US);
      digitalWrite(PIN_OUT, LOW);
      // remain idle for remainder of bin
      if (BIN_US > PULSE_US) delayMicroseconds(BIN_US - PULSE_US);
    } else {
      // idle for full bin
      delayMicroseconds(BIN_US);
    }
  }
  digitalWrite(PIN_OUT, LOW);
}

// -------------------- UI helpers --------------------
void show_status() {
  const ProtocolEntry &p = protocols[active_index];
  Serial.printf("ACTIVE: %s (id=%u)  SIDE: %s\n", p.name, p.id, active_side_opfor ? "OPFOR" : "BLUFOR");
  digitalWrite(PIN_LED_BLU, active_side_opfor ? LOW : HIGH);
  digitalWrite(PIN_LED_OPF, active_side_opfor ? HIGH : LOW);
}

void next_protocol() {
  active_index = (active_index + 1) % NUM_PROTOCOLS;
  Serial.printf("Selected protocol: %s\n", protocols[active_index].name);
  save_settings();
  show_status();
}

void toggle_side() {
  active_side_opfor = !active_side_opfor;
  Serial.printf("Switched side to: %s\n", active_side_opfor ? "OPFOR" : "BLUFOR");
  save_settings();
  show_status();
}

void perform_fire() {
  uint8_t bits[64];
  size_t bits_len = 0;
  const MILES_Code *code = protocols[active_index].code;
  if (!code) {
    Serial.println("Protocol has no code data");
    return;
  }
  build_frame_from_code(code, bits, &bits_len);
  apply_side_to_frame(bits, bits_len, active_side_opfor);
  // Fire
  laser_transmit_frame(bits, bits_len);
  // TODO: log shot to RAM/SD for AAR
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) ; // allow Serial to init briefly

  pinMode(PIN_OUT, OUTPUT);
  digitalWrite(PIN_OUT, LOW);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_SIDE, INPUT_PULLUP);
  pinMode(PIN_BTN_FIRE, INPUT_PULLUP);
  pinMode(PIN_LED_BLU, OUTPUT);
  pinMode(PIN_LED_OPF, OUTPUT);

  // EEPROM init
  eeprom_ok = false;
  if (EEPROM.begin(512)) {
    eeprom_ok = true;
    Serial.println("EEPROM initialized");
  } else {
    Serial.println("EEPROM init failed - settings will not persist");
  }

  load_settings();
  show_status();
}

void loop() {
  // Buttons are active-low
  if (digitalRead(PIN_BTN_NEXT) == LOW) {
    if (millis() - last_next_press > DEBOUNCE_MS) {
      last_next_press = millis();
      next_protocol();
    }
  }

  if (digitalRead(PIN_BTN_SIDE) == LOW) {
    if (millis() - last_side_press > DEBOUNCE_MS) {
      last_side_press = millis();
      toggle_side();
    }
  }

  if (digitalRead(PIN_BTN_FIRE) == LOW) {
    if (millis() - last_fire_press > DEBOUNCE_MS) {
      last_fire_press = millis();
      perform_fire();
    }
  }

  // Idle small sleep
  delay(10);
}
