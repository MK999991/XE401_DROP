/*
  RP2040 MILES Controller with OLED GUI + FSM + Shot counter + IR self-sense confirmation

  FSM (matches your block diagram):
    SAFE_STATE  -> (Power long-press) -> SAFE_READY
    SAFE_READY  -> (limit pressed)     -> ARMED_FLY
    ARMED_FLY   -> (limit released)    -> ARMED_SENSING
    ARMED_SENSING -> (altitude >= 3m)  -> ARMED_IR_FLASH
    ARMED_IR_FLASH -> (after TX)       -> EXPENDED
    EXPENDED (5 s) -> SAFE_STATE
    Power long-press from anywhere forces SAFE; from SAFE it arms to SAFE_READY.

  GUI:
    - Shows state, protocol, BLU/OPFOR, limit, ALT>=3m
    - Shot counter (#)
    - “IR FLASHED” toast on transmit
    - “CONFIRMED” indicator if self-sense sees the burst

  Timing:
    BIN_US / PULSE_US are demo values. Replace with your MILES timing.
    SIDE_BIT_INDEX = 5 (flip if your format uses a different team bit position).

  Transmit:
    laser_transmit_frame(...) is a simple stub (digitalWrite + delay).
    Swap in your precise PWM+DMA streaming routine here for real range use.
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MILES_CODES_H.h"

// -------------------- Pins --------------------
// IR out (to emitter driver transistor)
const uint8_t PIN_OUT       = 8;   // GP8

// Buttons (to GND, using INPUT_PULLUP)
const uint8_t PIN_BTN_PWR   = 10;  // Power/Arm long-press
const uint8_t PIN_BTN_NEXT  = 2;   // Next protocol
const uint8_t PIN_BTN_SIDE  = 3;   // Toggle BLU/OPFOR
const uint8_t PIN_BTN_FIRE  = 4;   // Manual fire (only from ARMED_SENSING)

// Inputs
const uint8_t PIN_LIMIT     = 6;   // Limit switch (HIGH = pressed) -> invert if needed
const uint8_t PIN_ALT_OK    = 7;   // Digital: HIGH means altitude >= 3m (replace with real sensor logic)
const uint8_t PIN_IR_SENSE  = 18;  // IR self-sense digital input (receiver module pointed at emitter)

// State LEDs
const uint8_t LED_SAFE      = 14;  // Green
const uint8_t LED_ARMED     = 15;  // Orange
const uint8_t LED_EXPENDED  = 16;  // Red

// OLED I2C (SSD1306, addr 0x3C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
const uint8_t I2C_SDA = 4;  // GP4
const uint8_t I2C_SCL = 5;  // GP5

// -------------------- MILES timing (demo) --------------------
const uint32_t BIN_US   = 500;  // bin duration (adjust!)
const uint32_t PULSE_US = 250;  // '1' pulse width inside a bin (adjust!)

// Team bit index (which bit of 11-bit frame encodes BLU/OPFOR)
const int SIDE_BIT_INDEX = 5;   // adjust to your protocol/receiver mapping

// -------------------- EEPROM --------------------
const int EEPROM_ADDR_MAGIC    = 0;
const int EEPROM_ADDR_PROTOCOL = 4;
const int EEPROM_ADDR_SIDE     = 8;
const uint32_t EEPROM_MAGIC    = 0x4D494C45; // 'MILE'

// -------------------- Protocol registry --------------------
typedef struct {
  uint8_t id;
  const char *name;
  const MILES_Code *code;
} ProtocolEntry;

// Codes from header
extern const MILES_Code PLAYER_UNIVERSAL_KILL;
extern const MILES_Code PLAYER_ID_001;
extern const MILES_Code PLAYER_ID_002;
extern const MILES_Code EVENT_PAUSE;
extern const MILES_Code EVENT_END_EXERCISE;

ProtocolEntry protocols[] = {
  { 0, "Universal Kill (Basic)", &PLAYER_UNIVERSAL_KILL },
  { 1, "Player ID 001",          &PLAYER_ID_001 },
  { 2, "Player ID 002",          &PLAYER_ID_002 },
  { 3, "Pause/Reset",            &EVENT_PAUSE },
  { 4, "End Exercise",           &EVENT_END_EXERCISE }
};
const size_t NUM_PROTOCOLS = sizeof(protocols) / sizeof(protocols[0]);

// -------------------- FSM --------------------
enum State : uint8_t {
  SAFE_STATE = 0,
  SAFE_READY,
  ARMED_FLY,
  ARMED_SENSING,
  ARMED_IR_FLASH,
  EXPENDED
};
State state = SAFE_STATE;

// -------------------- UI / settings --------------------
size_t active_index = 0;         // selected protocol index
bool active_side_opfor = false;  // false=BLUFOR, true=OPFOR
bool eeprom_ok = false;

// Debounce / holds
unsigned long t_last_next=0, t_last_side=0, t_last_fire=0, t_last_pwr=0;
const unsigned long DEBOUNCE_MS = 200;
const unsigned long PWR_HOLD_MS = 800;

// Expended timer
unsigned long t_expended_start = 0;
const unsigned long EXPENDED_MS = 5000;

// ---- Fire feedback / confirmation ----
volatile bool flash_event = false;        // set when TX happens
unsigned long flash_event_ms = 0;
uint32_t shot_count = 0;
const unsigned long FLASH_TOAST_MS = 600; // "IR FLASHED" banner duration

volatile bool flash_confirmed = false;    // set if self-sense sees the burst
unsigned long confirmed_ms = 0;
const unsigned long CONFIRM_WINDOW_MS = 12; // ms window after TX to accept confirmation
const unsigned long CONFIRM_SHOW_MS   = 800;

// -------------------- Persistence --------------------
void save_settings() {
  if (!eeprom_ok) return;
  EEPROM.put(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.put(EEPROM_ADDR_PROTOCOL, (uint8_t)protocols[active_index].id);
  EEPROM.put(EEPROM_ADDR_SIDE, (uint8_t)(active_side_opfor ? 1 : 0));
  EEPROM.commit();
}
void load_settings() {
  if (!eeprom_ok) return;
  uint32_t magic=0; EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  if (magic != EEPROM_MAGIC) return;
  uint8_t pid=0, side=0;
  EEPROM.get(EEPROM_ADDR_PROTOCOL, pid);
  EEPROM.get(EEPROM_ADDR_SIDE, side);
  for (size_t i=0;i<NUM_PROTOCOLS;i++) if (protocols[i].id==pid) { active_index=i; break; }
  active_side_opfor = (side!=0);
}

// -------------------- Frame helpers --------------------
void build_frame_from_code(const MILES_Code *c, uint8_t *out_bits, size_t *out_len) {
  for (size_t i=0;i<11;i++) out_bits[i] = c->pattern[i] ? 1 : 0;
  *out_len = 11;
}
void apply_side_to_frame(uint8_t *bits, size_t len, bool opfor) {
  if (len > (size_t)SIDE_BIT_INDEX) bits[SIDE_BIT_INDEX] = opfor ? 1 : 0;
}

// -------------------- Sensors --------------------
bool limit_switch_pressed() {
  // If wired INPUT_PULLUP->GND, invert this logic accordingly.
  // Here we assume HIGH = pressed (adjust for your wiring).
  return digitalRead(PIN_LIMIT) == HIGH;
}
bool altitude_ge_3m() {
  // For bench test, PIN_ALT_OK HIGH means ">= 3m".
  // Replace with actual baro/ultrasonic threshold logic when integrating real sensor.
  return digitalRead(PIN_ALT_OK) == HIGH;
}

// -------------------- Transmit (replace with DMA/PWM) --------------------
void laser_transmit_frame(const uint8_t *frame_bits, size_t bitlen) {
  // GUI feedback: shot count + toast
  shot_count++;
  flash_event = true;
  flash_event_ms = millis();

  Serial.print("TX bits: "); for (size_t i=0;i<bitlen;i++) Serial.print(frame_bits[i]?'1':'0'); Serial.println();

  // Simple, illustrative timing only. Replace with your PWM+DMA burst.
  digitalWrite(PIN_OUT, LOW);
  delayMicroseconds(10);

  for (size_t i=0;i<bitlen;i++) {
    if (frame_bits[i]) {
      digitalWrite(PIN_OUT, HIGH);
      delayMicroseconds(PULSE_US);
      digitalWrite(PIN_OUT, LOW);
      if (BIN_US > PULSE_US) delayMicroseconds(BIN_US - PULSE_US);
    } else {
      delayMicroseconds(BIN_US);
    }
  }
  digitalWrite(PIN_OUT, LOW);

  // Physical confirmation window: check self-sense pin for a brief time
  unsigned long start = millis();
  bool seen = false;
  while (millis() - start < CONFIRM_WINDOW_MS) {
    // Adjust polarity to match your IR module. Many produce HIGH on envelope detect.
    if (digitalRead(PIN_IR_SENSE) == HIGH) { seen = true; break; }
  }
  flash_confirmed = seen;
  confirmed_ms = millis();
}

// -------------------- LEDs & GUI --------------------
void set_state_leds() {
  digitalWrite(LED_SAFE,     state == SAFE_STATE ? HIGH : LOW);
  digitalWrite(LED_ARMED,   (state == SAFE_READY || state == ARMED_FLY || state == ARMED_SENSING || state == ARMED_IR_FLASH) ? HIGH : LOW);
  digitalWrite(LED_EXPENDED, state == EXPENDED ? HIGH : LOW);
}
const char* state_name(State s) {
  switch(s) {
    case SAFE_STATE:     return "SAFE";
    case SAFE_READY:     return "SAFE READY";
    case ARMED_FLY:      return "ARMED FLY";
    case ARMED_SENSING:  return "ARMED SENSE";
    case ARMED_IR_FLASH: return "IR FLASH";
    case EXPENDED:       return "EXPENDED";
    default:             return "?";
  }
}
void draw_gui() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setTextSize(1);
  display.setCursor(0,0); display.print("MILES FSM");

  // Shot counter (top-right)
  display.setCursor(98, 0); display.print("#"); display.print(shot_count);

  // State
  display.setCursor(0, 12); display.print("State:");
  display.setTextSize(2);
  display.setCursor(48, 10); display.print(state_name(state));
  display.setTextSize(1);

  // Protocol & side
  display.setCursor(0, 32); display.print("Proto: "); display.print(protocols[active_index].name);
  display.setCursor(0, 44); display.print("Side : "); display.print(active_side_opfor ? "OPFOR" : "BLUFOR");

  // Inputs
  display.setCursor(0, 56);
  display.print("LIM:");   display.print(limit_switch_pressed() ? "ON "  : "OFF");
  display.print(" ALT3m:"); display.print(altitude_ge_3m()     ? "YES" : "NO ");

  // Toast: “IR FLASHED”
  if (flash_event && (millis() - flash_event_ms) < FLASH_TOAST_MS) {
    display.fillRect(0, 24, 128, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(28, 24); display.print("IR FLASHED");
    display.setTextColor(SSD1306_WHITE);
  } else if (flash_event && (millis() - flash_event_ms) >= FLASH_TOAST_MS) {
    flash_event = false;
  }

  // Confirmation: show for a short time after TX
  if (flash_confirmed && (millis() - confirmed_ms) < CONFIRM_SHOW_MS) {
    display.setCursor(0, 24); display.print("CONFIRMED");
  } else if (flash_confirmed && (millis() - confirmed_ms) >= CONFIRM_SHOW_MS) {
    flash_confirmed = false;
  }

  // Expended countdown
  if (state == EXPENDED) {
    unsigned long remain = 0;
    if (millis() - t_expended_start < EXPENDED_MS)
      remain = (EXPENDED_MS - (millis() - t_expended_start)) / 1000UL;
    display.setCursor(100, 56); display.print("T-"); display.print(remain); display.print("s");
  }

  display.display();
  set_state_leds();
}

// -------------------- Buttons / actions --------------------
void next_protocol()       { active_index = (active_index + 1) % NUM_PROTOCOLS; save_settings(); draw_gui(); }
void toggle_side()         { active_side_opfor = !active_side_opfor; save_settings(); draw_gui(); }
void manual_fire()         { if (state == ARMED_SENSING) { state = ARMED_IR_FLASH; draw_gui(); } }
// Power long-press (from SAFE -> SAFE_READY; otherwise -> SAFE)
void handle_power_button() {
  static bool pwr_down = false;
  if (digitalRead(PIN_BTN_PWR) == LOW) {
    if (!pwr_down) { pwr_down = true; t_last_pwr = millis(); }
    if (pwr_down && (millis() - t_last_pwr >= PWR_HOLD_MS)) {
      if (state == SAFE_STATE) state = SAFE_READY;
      else                     state = SAFE_STATE;
      pwr_down = false;
      t_expended_start = 0;
      draw_gui();
    }
  } else {
    pwr_down = false;
  }
}

// -------------------- FSM step --------------------
void fsm_step() {
  switch (state) {
    case SAFE_STATE:
      break;

    case SAFE_READY:
      if (limit_switch_pressed()) { state = ARMED_FLY; draw_gui(); }
      break;

    case ARMED_FLY:
      if (!limit_switch_pressed()) { state = ARMED_SENSING; draw_gui(); }
      break;

    case ARMED_SENSING:
      if (altitude_ge_3m()) { state = ARMED_IR_FLASH; draw_gui(); }
      break;

    case ARMED_IR_FLASH: {
      uint8_t bits[64]; size_t n=0;
      build_frame_from_code(protocols[active_index].code, bits, &n);
      apply_side_to_frame(bits, n, active_side_opfor);
      laser_transmit_frame(bits, n);
      state = EXPENDED;
      t_expended_start = millis();
      draw_gui();
    } break;

    case EXPENDED:
      if (millis() - t_expended_start >= EXPENDED_MS) { state = SAFE_STATE; draw_gui(); }
      break;
  }
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(115200);
  while(!Serial && millis() < 1500);

  pinMode(PIN_OUT, OUTPUT); digitalWrite(PIN_OUT, LOW);

  pinMode(PIN_BTN_PWR,  INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_SIDE, INPUT_PULLUP);
  pinMode(PIN_BTN_FIRE, INPUT_PULLUP);

  pinMode(PIN_LIMIT,   INPUT);     // set to INPUT_PULLUP if wired to GND
  pinMode(PIN_ALT_OK,  INPUT);
  pinMode(PIN_IR_SENSE,INPUT);     // IR self-sense module (adjust polarity test above)

  pinMode(LED_SAFE, OUTPUT);
  pinMode(LED_ARMED, OUTPUT);
  pinMode(LED_EXPENDED, OUTPUT);
  set_state_leds();

  eeprom_ok = EEPROM.begin(512);
  if (!eeprom_ok) Serial.println("EEPROM init failed (settings won’t persist)");
  load_settings();

  Wire.setSDA(I2C_SDA); Wire.setSCL(I2C_SCL); Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed at 0x3C");
  } else {
    display.clearDisplay(); display.display();
  }

  draw_gui();
}

void loop() {
  handle_power_button();

  if (digitalRead(PIN_BTN_NEXT) == LOW) {
    if (millis() - t_last_next > DEBOUNCE_MS) { t_last_next = millis(); next_protocol(); }
  }
  if (digitalRead(PIN_BTN_SIDE) == LOW) {
    if (millis() - t_last_side > DEBOUNCE_MS) { t_last_side = millis(); toggle_side(); }
  }
  if (digitalRead(PIN_BTN_FIRE) == LOW) {
    if (millis() - t_last_fire > DEBOUNCE_MS) { t_last_fire = millis(); manual_fire(); }
  }

  fsm_step();
  delay(5);
}
