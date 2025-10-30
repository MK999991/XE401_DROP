#!/usr/bin/env python3
"""
MILES OLED Simulator (no hardware required)
- Uses tkinter to render a 128x64 OLED (scaled up)
- Simulates the FSM states and the on-device GUI
- Keyboard controls:
    P : Power long-press (SAFE <-> SAFE READY; from any other state -> SAFE)
    N : Next protocol
    S : Toggle side (BLUFOR/OPFOR)
    L : Toggle limit switch (pressed/released)
    A : Toggle altitude OK (>= 3 m)
    F : Manual fire (only from ARMED SENSING -> IR FLASH)
    R : Reset to SAFE
    Q : Quit

"""

import tkinter as tk
import time

# --- OLED logical size and scale factor for the desktop window ---
OLED_W, OLED_H = 128, 64
SCALE = 4  # window pixels per OLED pixel
W, H = OLED_W * SCALE, OLED_H * SCALE

# --- FSM States ---
SAFE_STATE     = 0
SAFE_READY     = 1
ARMED_FLY      = 2
ARMED_SENSING  = 3
ARMED_IR_FLASH = 4
EXPENDED       = 5

STATE_NAMES = {
    SAFE_STATE: "SAFE",
    SAFE_READY: "SAFE READY",
    ARMED_FLY: "ARMED FLY",
    ARMED_SENSING: "ARMED SENSE",
    ARMED_IR_FLASH: "IR FLASH",
    EXPENDED: "EXPENDED"
}

# --- Protocol registry (demo) ---
PROTOCOLS = [
    "Universal Kill (Basic)",
    "Player ID 001",
    "Player ID 002",
    "Pause/Reset",
    "End Exercise"
]

# --- Timings ---
EXPENDED_MS = 5000

class MilesSim:
    def __init__(self, root):
        self.root = root
        self.root.title("MILES OLED Simulator (128x64)")
        self.canvas = tk.Canvas(root, width=W, height=H, bg="black", highlightthickness=0)
        self.canvas.pack()

        # State
        self.state = SAFE_STATE
        self.active_index = 0
        self.side_opfor = False  # False=BLUFOR, True=OPFOR

        # Inputs
        self.limit_pressed = False
        self.altitude_ok = False

        # Timers
        self.expended_start = None

        # Bind keys
        root.bind("<Key>", self.on_key)

        # Start render/update loop
        self.last_update = time.time()
        self.update_loop()

    # --- Key handling ---
    def on_key(self, event):
        k = event.keysym.lower()
        if k == 'q':
            self.root.destroy()
        elif k == 'p':
            self.handle_power_hold()
        elif k == 'n':
            self.active_index = (self.active_index + 1) % len(PROTOCOLS)
        elif k == 's':
            self.side_opfor = not self.side_opfor
        elif k == 'l':
            self.limit_pressed = not self.limit_pressed
        elif k == 'a':
            self.altitude_ok = not self.altitude_ok
        elif k == 'f':
            self.manual_fire()
        elif k == 'r':
            self.reset_safe()

    # --- FSM helpers ---
    def handle_power_hold(self):
        # From SAFE -> SAFE_READY; from anywhere else -> SAFE
        if self.state == SAFE_STATE:
            self.state = SAFE_READY
        else:
            self.state = SAFE_STATE
            self.expended_start = None
            self.altitude_ok = False
            self.limit_pressed = False

    def manual_fire(self):
        if self.state == ARMED_SENSING:
            self.state = ARMED_IR_FLASH

    def reset_safe(self):
        self.state = SAFE_STATE
        self.expended_start = None
        self.altitude_ok = False
        self.limit_pressed = False

    def fsm_step(self):
        if self.state == SAFE_STATE:
            pass
        elif self.state == SAFE_READY:
            if self.limit_pressed:
                self.state = ARMED_FLY
        elif self.state == ARMED_FLY:
            if not self.limit_pressed:
                self.state = ARMED_SENSING
        elif self.state == ARMED_SENSING:
            if self.altitude_ok:
                self.state = ARMED_IR_FLASH
        elif self.state == ARMED_IR_FLASH:
            # "Transmit" once, then go EXPENDED
            self.state = EXPENDED
            self.expended_start = time.time()
        elif self.state == EXPENDED:
            if self.expended_start and (time.time() - self.expended_start) >= (EXPENDED_MS / 1000.0):
                self.state = SAFE_STATE
                self.expended_start = None

    # --- Rendering ---
    def draw_text(self, x, y, text, size=1, invert=False):
        # Very simple text rendering using canvas.create_text, scaled to OLED coords
        color = "black" if invert else "white"
        # approximate font sizes: size=1 => 8px, size=2 => 16px
        font_size = 8 * size * SCALE // 2  # tweak for readability
        self.canvas.create_text(x*SCALE, y*SCALE, anchor='nw',
                                text=text, fill=color, font=("Courier", font_size))

    def render(self):
        self.canvas.delete("all")
        # border to mimic OLED
        self.canvas.create_rectangle(0, 0, W-1, H-1, outline="#444", width=1)

        # Title
        self.draw_text(0, 0, "MILES FSM", size=1)

        # State
        self.draw_text(0, 10, "State:", size=1)
        self.draw_text(48, 8, STATE_NAMES[self.state], size=2)

        # Protocol & Side
        self.draw_text(0, 28, "Proto: " + PROTOCOLS[self.active_index], size=1)
        self.draw_text(0, 38, "Side : " + ("OPFOR" if self.side_opfor else "BLUFOR"), size=1)

        # Inputs
        self.draw_text(0, 50, f"LIM:{'ON ' if self.limit_pressed else 'OFF'} ALT3m:{'YES' if self.altitude_ok else 'NO '}", size=1)

        # Expended countdown
        if self.state == EXPENDED and self.expended_start:
            remain = max(0, int(EXPENDED_MS/1000 - (time.time() - self.expended_start)))
            self.draw_text(100, 50, f"T-{remain}s", size=1)

        # Footer help
        self.draw_text(0, 58, "P N S L A F R Q", size=1)

        # Fake LEDs (blocks at right)
        # SAFE (green), ARMED (orange), EXPENDED (red)
        def led(x, y, on, color):
            c = color if on else "#222"
            self.canvas.create_rectangle(x*SCALE, y*SCALE, (x+6)*SCALE, (y+6)*SCALE, fill=c, outline=c)
        led(114, 2,  self.state == SAFE_STATE,    "#2ecc71")  # green
        led(114, 10, self.state in (SAFE_READY, ARMED_FLY, ARMED_SENSING, ARMED_IR_FLASH), "#f39c12")  # orange
        led(114, 18, self.state == EXPENDED,      "#e74c3c")  # red

    def update_loop(self):
        # Step FSM, then render ~30 FPS
        self.fsm_step()
        self.render()
        self.root.after(33, self.update_loop)  # 30 Hz

def main():
    root = tk.Tk()
    app = MilesSim(root)
    root.mainloop()

if __name__ == "__main__":
    main()
