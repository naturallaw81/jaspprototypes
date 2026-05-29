# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A collection of open hardware prototypes by Jasp / Multifex — 3D-printable models (`.stl`, `.3mf`), bills of materials, wiring diagrams, and Arduino firmware. It is **not** a software project: there is no package manager, build system, lint, or test suite. The only compiled artifact is the joystick firmware sketch, which is built and flashed through the Arduino IDE.

Two independent projects live in their own top-level directories:
- `jasp-keyboard-joystick/` — single analog thumbstick on a Pro Micro (the only directory with firmware/code).
- `reduced-height-dactyl-split-keyboard/` — print files and BOM only, no firmware.

License is CC BY-NC-SA 4.0 (non-commercial).

## The joystick firmware (`jasp-keyboard-joystick/firmware/`)

This is where almost all editable code lives.

- **`sketch_promicro.ino`** — the firmware. Target board is **Arduino Leonardo** in the IDE (the Pro Micro shares the ATmega32U4 / native USB HID).
- **`web_config.html`** — a single-file Web Serial GUI (vanilla JS, no build step) for configuring a flashed board from Chrome/Edge.
- **`Firmware_Installation_Guide.md`** — install/flash/configure steps for end users.

### Building & flashing (no CLI build — Arduino IDE only)
1. Open `sketch_promicro.ino` in the Arduino IDE, select board **Arduino Leonardo**.
2. Install the only third-party dependency: **MHeironimus/ArduinoJoystickLibrary** via *Sketch → Include Library → Add .ZIP Library*. `Mouse.h` and `EEPROM.h` ship with the IDE.
3. Compile (checkmark), upload (arrow).

### Testing the GUI locally
Web Serial requires a secure context, so `file://` won't work — serve it:
```bash
cd jasp-keyboard-joystick/firmware
python3 -m http.server 8000
# open http://localhost:8000/web_config.html in Chrome/Edge
```

## Architecture: the serial protocol is the contract

The firmware and `web_config.html` are coupled only through a **line-based serial protocol at 115200 baud**. There is no shared header or codegen — if you change the protocol on one side you must change the other by hand. Three clients speak it interchangeably: the bundled GUI, the Chrome Labs serial terminal, and the Arduino Serial Monitor.

- **Commands (host → board):** `help`, `status`, `cal`, `mode j|m`, `dz <0-200>`, `sens <1-30>`, `invx 0|1`, `invy 0|1`, `stream 0|1`, `reset`. Parsed in `processSerial()` via `startsWith`/equality — prefix collisions matter (e.g. `invx` is matched before a bare `inv`).
- **Replies (board → host):** `KEY=VALUE` lines (`MODE=`, `CENTER=`, `DEADZONE=`, etc.), which the GUI parses to sync its widgets.
- **Telemetry:** when `stream 1` is active the board emits `DATA x,y,btn,mode` every ~50 ms; the GUI plots the live stick position from this. The GUI sends `stream 1` on connect and `stream 0` on disconnect.

### Firmware behavior worth knowing before editing
- **Persistent config** lives in a `Config` struct written to EEPROM at offset 0, guarded by a magic number (`0x4A53`). On boot, a mismatched magic triggers `resetDefaults()`. **If you add or reorder `Config` fields, bump `CONFIG_MAGIC`** or already-flashed boards will load garbage.
- **Two HID modes** (`cfg.mode`): joystick (2 axes + 1 button via `Joystick_`) and mouse (`Mouse.move` with a sign-preserving quadratic response curve `x*|x|` and a sub-pixel accumulator). The inactive HID is held centered/released so it can't emit stuck input.
- **Mode switching is serial-only by design** — deliberately not bound to the button, because a long-press gesture would conflict with mouse drag.
- Hardware pins: `X_PIN=A1`, `Y_PIN=A0`, `BTN_PIN=14` (INPUT_PULLUP, active-low). Main `loop()` polls at ~125 Hz (`delay(8)`).
