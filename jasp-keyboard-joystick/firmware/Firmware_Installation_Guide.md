# Firmware Installation Guide

## Prerequisites
- Download and install [Arduino IDE](https://www.arduino.cc/en/software/)
- During first launch, allow Arduino IDE to install drivers when prompted

## Setup

1. Open [sketch_promicro.ino](https://github.com/multifex/prototypes/blob/main/jasp-keyboard-joystick/firmware/sketch_promicro.ino) in Arduino IDE
2. Plug your Pro Micro into your PC
3. In the Arduino IDE board dropdown, select **Arduino Leonardo**

## Install the Joystick Library

1. Go to [github.com/MHeironimus/ArduinoJoystickLibrary](https://github.com/MHeironimus/ArduinoJoystickLibrary)
2. Click **Code → Download ZIP**
3. In Arduino IDE, go to **Sketch → Include Library → Add .ZIP Library**
4. Select the ZIP you downloaded

> `Mouse.h` and `EEPROM.h` are bundled with the Arduino IDE — no extra install needed.

## Compile & Upload

1. Press the **checkmark icon** to compile the firmware
2. Press the **arrow icon** to upload it to your board

## Verify It's Working

1. Open the Windows Start menu and search for **"Set up USB game controllers"**
2. **Arduino Leonardo** should appear in the list
3. Click **Properties** and confirm the analog joystick is responding

## Configure (after upload)

The firmware exposes a text-based serial command interface at **115200 baud** that persists all settings to EEPROM. Pick one of the three UIs below — all speak the same protocol.

### Option 1 — Web Serial Terminal (no install, existing tool)
Chrome team's hosted Web Serial terminal works as-is:

1. Open **https://googlechromelabs.github.io/serial-terminal/** in Chrome or Edge
2. Set baud rate to **115200**, click **Connect**, pick the Pro Micro port
3. Type `help` and press Enter

### Option 2 — Bundled GUI (`web_config.html`)
A single-file HTML UI with sliders, checkboxes, and a live joystick visualization is included next to the firmware. Web Serial requires a secure context, so serve it locally:

```bash
cd jasp-keyboard-joystick/firmware
python3 -m http.server 8000
# then open http://localhost:8000/web_config.html in Chrome / Edge
```

Click **Connect**, pick the Pro Micro port, and the UI auto-syncs with the current EEPROM config.

### Option 3 — Arduino IDE Serial Monitor
Always available as a fallback. Set the bottom-right baud rate to **115200**, then send commands the same way.

### Serial Command Reference

| Command | Effect |
|---|---|
| `help` | Print the command list |
| `status` | Print all current settings |
| `cal` | Sample the resting position and store as new center |
| `mode j` / `mode m` | Switch to **j**oystick or **m**ouse mode |
| `dz <0–200>` | Deadzone in ADC counts (default 15) |
| `sens <1–30>` | Mouse-mode cursor speed (default 10) |
| `invx 0\|1` | Invert X axis |
| `invy 0\|1` | Invert Y axis |
| `stream 0\|1` | Toggle 50 ms telemetry (used by the GUI) |
| `reset` | Restore factory defaults |

> **Mode toggle is serial-only.** A long-press button gesture would conflict with mouse drag-and-drop, so mode switching is intentionally not bound to the physical button.

## Game Compatibility

Some (probably most) games may not recognize the joystick directly. If that happens, try one of these key remappers:

- [AntiMicroX](https://github.com/antimicroX/antimicroX) — maps joystick inputs to keyboard/mouse
- [x360ce](https://github.com/x360ce/x360ce) — emulates an Xbox 360 controller

For pointer control in any app, switch the device to **mouse mode** instead (`mode m`) — no remapper required.
