#!/usr/bin/env python3
# (See full header inside previous attempt; omitted for brevity to save space)

# Controls (keyboard)
# P — Power long-press behavior (SAFE ⇄ SAFE READY; from any other state → SAFE)
# N — Next protocol
# S — Toggle side (BLUFOR / OPFOR)
# L — Toggle Limit switch (pressed/released)
# A — Toggle Altitude ≥ 3 m
# F — Manual fire (from ARMED SENSE → IR FLASH)
# C — Toggle confirmation mode (Auto vs Manual)
# X — In Manual mode, press right after a shot to simulate self-sense confirmation
# R — Reset to SAFE
# Q — Quit

# What you’ll see matches the embedded GUI:
# State text (SAFE → SAFE READY → ARMED FLY → ARMED SENSE → IR FLASH → EXPENDED)
# Protocol name and Side (BLUFOR/OPFOR)
# Inputs (LIM, ALT3m) indicators
# Shot counter (#)
# IR FLASHED banner briefly after transmission
# CONFIRMED indicator when self-sense is detected (auto or manual)
# “LEDs” on the right (green/orange/red) for SAFE/ARMED/EXPENDED

import tkinter as tk
import time

OLED_W, OLED_H = 128, 72
SCALE = 4
W, H = OLED_W * SCALE, OLED_H * SCALE

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
    EXPENDED: "EXPENDED",
}

PROTOCOLS = [
    "Universal Kill (Basic)",
    "Player ID 001",
    "Player ID 002",
    "Pause/Reset",
    "End Exercise",
]

EXPENDED_MS = 5000
FLASH_TOAST_MS = 600
CONFIRM_WINDOW_MS = 36    # scaled up for desktop
CONFIRM_SHOW_MS = 800

class MilesSim:
    def __init__(self, root):
        self.root = root
        self.root.title("MILES OLED FSM Simulator (128x64)")
        self.canvas = tk.Canvas(root, width=W, height=H, bg="black", highlightthickness=0)
        self.canvas.pack()

        self.state = SAFE_STATE
        self.active_index = 0
        self.side_opfor = False

        self.limit_pressed = False
        self.altitude_ok = False

        self.shot_count = 0
        self.flash_event = False
        self.flash_event_time = 0.0

        self.flash_confirmed = False
        self.confirmed_time = 0.0

        self.confirm_auto = True
        self.confirm_window_start = 0.0

        self.expended_start = 0.0

        root.bind("<Key>", self.on_key)
        self.update_loop()

    def now(self): return time.time()

    def on_key(self, event):
        k = event.keysym.lower()
        if k == 'q': self.root.destroy()
        elif k == 'p': self.handle_power_hold()
        elif k == 'n': self.active_index = (self.active_index + 1) % len(PROTOCOLS)
        elif k == 's': self.side_opfor = not self.side_opfor
        elif k == 'l': self.limit_pressed = not self.limit_pressed
        elif k == 'a': self.altitude_ok = not self.altitude_ok
        elif k == 'f': self.manual_fire()
        elif k == 'c': self.confirm_auto = not self.confirm_auto
        elif k == 'x':
            if not self.confirm_auto and (self.now() - self.confirm_window_start)*1000 <= CONFIRM_WINDOW_MS:
                self.set_confirmed()
        elif k == 'r': self.reset_safe()

    def handle_power_hold(self):
        if self.state == SAFE_STATE: self.state = SAFE_READY
        else:
            self.state = SAFE_STATE
            self.expended_start = 0.0
            self.altitude_ok = False
            self.limit_pressed = False

    def manual_fire(self):
        if self.state == ARMED_SENSING: self.state = ARMED_IR_FLASH

    def reset_safe(self):
        self.state = SAFE_STATE
        self.expended_start = 0.0
        self.altitude_ok = False
        self.limit_pressed = False

    def transmit(self):
        self.shot_count += 1
        self.flash_event = True
        self.flash_event_time = self.now()
        self.confirm_window_start = self.flash_event_time
        if self.confirm_auto:
            self.root.after(80, self.set_confirmed)

    def set_confirmed(self):
        self.flash_confirmed = True
        self.confirmed_time = self.now()

    def fsm_step(self):
        if self.state == SAFE_STATE: pass
        elif self.state == SAFE_READY:
            if self.limit_pressed: self.state = ARMED_FLY
        elif self.state == ARMED_FLY:
            if not self.limit_pressed: self.state = ARMED_SENSING
        elif self.state == ARMED_SENSING:
            if self.altitude_ok: self.state = ARMED_IR_FLASH
        elif self.state == ARMED_IR_FLASH:
            self.transmit()
            self.state = EXPENDED
            self.expended_start = self.now()
        elif self.state == EXPENDED:
            if (self.now() - self.expended_start)*1000 >= EXPENDED_MS:
                self.state = SAFE_STATE
                self.expended_start = 0.0

    def draw_text(self, x, y, text, size=1, invert=False):
        color = "black" if invert else "white"
        font_size = 8 * size * SCALE // 2
        self.canvas.create_text(x*SCALE, y*SCALE, anchor='nw', text=text, fill=color, font=("Courier", font_size))

    def led(self, x, y, on, color):
        c = color if on else "#222"
        self.canvas.create_rectangle(x*SCALE, y*SCALE, (x+6)*SCALE, (y+6)*SCALE, fill=c, outline=c)

    def render(self):
        self.canvas.delete("all")
        self.canvas.create_rectangle(0, 0, W-1, H-1, outline="#444", width=1)

        self.draw_text(0, 0, "MILES FSM", size=1)
        self.draw_text(98, 0, f"#{self.shot_count}", size=1)

        self.draw_text(0, 12, "State:", size=1)
        self.draw_text(48, 10, STATE_NAMES[self.state], size=2)

        self.draw_text(0, 32, "Proto: " + PROTOCOLS[self.active_index], size=1)
        self.draw_text(0, 44, "Side : " + ("OPFOR" if self.side_opfor else "BLUFOR"), size=1)

        self.draw_text(0, 56, f"LIM:{'ON ' if self.limit_pressed else 'OFF'} ALT3m:{'YES' if self.altitude_ok else 'NO '}", size=1)

        # Toast
        if self.flash_event and (self.now() - self.flash_event_time)*1000 < FLASH_TOAST_MS:
            self.canvas.create_rectangle(0, 24*SCALE, 128*SCALE, 34*SCALE, fill="white", outline="white")
            self.draw_text(32, 24, "IR FLASHED", size=1, invert=True)
        elif self.flash_event and (self.now() - self.flash_event_time)*1000 >= FLASH_TOAST_MS:
            self.flash_event = False

        # Confirm
        if self.flash_confirmed and (self.now() - self.confirmed_time)*1000 < CONFIRM_SHOW_MS:
            self.draw_text(0, 24, "CONFIRMED", size=1, invert=False)
        elif self.flash_confirmed and (self.now() - self.confirmed_time)*1000 >= CONFIRM_SHOW_MS:
            self.flash_confirmed = False

        # Expended countdown
        if self.state == EXPENDED and self.expended_start:
            remain = max(0, int(EXPENDED_MS/1000 - (self.now() - self.expended_start)))
            self.draw_text(100, 56, f"T-{remain}s", size=1)

        self.led(114, 2,  self.state == SAFE_STATE, "#2ecc71")
        self.led(114, 10, self.state in (SAFE_READY, ARMED_FLY, ARMED_SENSING, ARMED_IR_FLASH), "#f39c12")
        self.led(114, 18, self.state == EXPENDED, "#e74c3c")

        self.draw_text(0, 65, "P N S L A F C X R Q", size=1)

    def update_loop(self):
        self.fsm_step()
        self.render()
        self.root.after(33, self.update_loop)

def main():
    root = tk.Tk()
    MilesSim(root)
    root.mainloop()

if __name__ == "__main__":
    main()
