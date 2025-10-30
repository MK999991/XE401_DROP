# XE401_DROP
# MILES FSM GUI Controller

This project implements a **MILES (Multiple Integrated Laser Engagement System) controller** using an RP2040 microcontroller and an OLED GUI.  
It also includes a **desktop simulator** (Python) so you can test the finite-state machine (FSM) and GUI without hardware.

---

## Features
- Finite State Machine (FSM) matching the training block diagram:
  - SAFE → SAFE READY → ARMED FLY → ARMED SENSE → IR FLASH → EXPENDED → SAFE
- OLED display (SSD1306, 128×64) shows:
  - Current state
  - Selected MILES protocol
  - BLUFOR / OPFOR side
  - Limit switch & altitude indicators
  - Expended countdown timer
- Buttons for protocol selection, side toggle, and power/arming
- EEPROM persistence for protocol and side
- IR emission stub (replace with DMA/PWM transmitter for real MILES timing)
- Python simulator for testing without hardware

---

## Hardware Setup (RP2040)

### Wiring
- **OLED SSD1306 (I²C, addr 0x3C)**
  - SDA → GP4  
  - SCL → GP5  
  - VCC → 3.3V  
  - GND → GND  
- **Buttons (to GND with INPUT_PULLUP)**
  - Power/Arm → GP10  
  - Next Protocol → GP2  
  - Side (BLU/OPFOR) → GP3  
  - Manual Fire → GP4  
- **Inputs**
  - Limit switch → GP6  
  - Altitude OK (≥3m digital) → GP7  
- **LEDs**
  - SAFE → GP14 (green)  
  - ARMED → GP15 (orange)  
  - EXPENDED → GP16 (red)  
- **IR Out**
  - GP8 → IR emitter driver transistor

### Arduino Setup
1. Install Arduino IDE
2. Install **Raspberry Pi RP2040** board support
3. Install required libraries:
   - Adafruit SSD1306
   - Adafruit GFX
4. Open `DROP_MILES.cpp` and upload

---

## Python OLED Simulator

If you don’t have hardware, test the GUI on your PC.

### Run
```bash
python3 MILES_GUI.py
