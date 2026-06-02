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
| `setgate <x0,y0,…,x7,y7>` | Write the 8-direction polar gate (16 int16 offsets from center) — produced by the GUI's "Calibrate Gate" flow; maps the physical extent in every direction to the unit circle |
| `mode j` / `mode m` / `mode s` | Switch to **j**oystick, **m**ouse, or **s**croll mode |
| `dz <0–200>` | Deadzone in ADC counts (default 15) |
| `sens <1–30>` | Mouse-mode cursor speed (default 10) |
| `invx 0\|1` | Invert X axis |
| `invy 0\|1` | Invert Y axis |
| `ssens <1–30>` | Scroll-mode speed (default 10) |
| `sinv 0\|1` | Invert scroll direction (natural scrolling) |
| `slock 0\|1` | Scroll axis lock — only the dominant axis scrolls (default 1) |
| `mbtn 0–4` | Mouse-mode button: 0 = no switch · 1 = 3-tap switch · 2 = 2-tap+hold switch (default) · 3 = hold switch (browsing) · 4 = fast tap (instant left button, gaming) |
| `sbtn 0\|1` | Scroll-mode return gesture: 0 = hold (default) · 1 = tap (instant) |
| `sidle <0–120>` | Scroll mode auto-returns to mouse after N seconds idle (0 = off, default) |
| `tap <150–600>` | Tap chaining window in ms, counted from each release (default 250). Hold threshold = window + 200 ms |
| `stream 0\|1` | Toggle 50 ms telemetry (used by the GUI) |
| `reset` | Restore factory defaults |

> **Joystick mode is serial-only.** Mouse ↔ scroll switching is bound to button tap gestures (see Scroll Mode below), but entering/leaving joystick mode requires a serial command or the GUI — a gesture would conflict with the game button.

## Game Compatibility

The joystick mode is a standard HID (DirectInput) gamepad. Older games, emulators, and many indies see it natively — verify with `joy.cpl` ("Set up USB game controllers"). Most modern games, however, are **XInput-only** (Xbox controller). Your options, easiest first:

- **Steam Input** — Steam → Settings → Controller → enable generic gamepad support, then map per game. Works for non-Steam games via "Add a Non-Steam Game".
- [AntiMicroX](https://github.com/antimicroX/antimicroX) — maps the stick to keyboard keys (e.g. WASD) and the button to any key. Games see plain keyboard input, so compatibility is 100%.
- [x360ce](https://github.com/x360ce/x360ce) — user-mode Xbox 360 controller emulation.
- **Native XInput firmware** — see the next section. The device *becomes* an Xbox 360 controller; no software layer at all.

For pointer control in any app, switch the device to **mouse mode** instead (`mode m`) — no remapper required.

## XInput Firmware (native Xbox 360 controller)

A second sketch, [`sketch_promicro_xinput`](sketch_promicro_xinput/sketch_promicro_xinput.ino), makes the board enumerate as a real Xbox 360 controller: stick → left analog stick, button → A. Games get native XInput with zero added latency.

**Trade-offs — read before flashing:**

| | Main firmware | XInput firmware |
|---|---|---|
| Joystick / mouse / scroll modes | ✅ | ❌ gamepad only |
| Web config / serial commands | ✅ | ❌ no USB serial (Xbox pads have no CDC) |
| Upload / re-flash | automatic | **manual reset required** (see below) |
| Calibration | set & stored via web config | **reused from EEPROM read-only** — calibrate on the main firmware first |

### Build requirements

1. Boards Manager URL (File → Preferences → Additional Boards Manager URLs):
   `https://raw.githubusercontent.com/dmadison/ArduinoXInput_Boards/master/package_dmadison_xinput_index.json`
2. Boards Manager → install **ArduinoXInput AVR Boards**
3. Library Manager → install **XInput** (David Madison)
4. Board selection: **Arduino Leonardo w/ XInput** (`xinput:avr:leonardo`)

### Flashing (both directions need this)

The XInput firmware has no USB serial, so the IDE cannot auto-reset the board into its bootloader:

1. Start the upload in the IDE / CLI
2. When "Uploading…" appears, **short the RST pin to GND twice quickly** (Pro Micro right-side pins, top-down: RAW · GND · **RST** · VCC — GND is directly above RST, so a tweezer tap across the two pins works)
3. The bootloader stays alive for ~8 seconds — the upload catches it

Going **back to the main firmware** uses the same procedure (select board *Arduino Leonardo* again first).

### Workflow

1. On the **main firmware**, calibrate center + gate in the web config (stored in EEPROM)
2. Flash the XInput firmware for a gaming session — it reads that calibration as-is
3. Flash the main firmware back when you want mouse/scroll/config again

## Scroll Mode

`mode s` turns the stick into a scroll wheel:

- **Stick up/down** — vertical scroll (wheel)
- **Stick left/right** — horizontal scroll (AC Pan, works in browsers / Office / editors that support tilt-wheel)
- **Button — deferred tap/hold gestures.** Nothing is sent until a gesture resolves, so taps can never leak clicks. The grammar:
  - **N quick taps = click of button N** (1 = left, 2 = right, 3 = middle)
  - **(N−1) taps + press held = HOLD of button N until release** (left drag, right drag, …)
  - Chaining window (`tap`, default 250 ms) counts from each tap's **release**; the hold tier engages after window + 200 ms of continuous press. Left/right clicks resolve one window after release; 3-tap actions resolve right at the third release.
  - `mbtn` picks where the switch to scroll mode lives (or disables gestures entirely):
    - **`0` — mouse only**: full L/R/M clicks + L/R/M holds, no on-device switch
    - **`1` — 3-tap switch**: 3 taps = scroll (middle click sacrificed; 2 taps+hold still = middle drag)
    - **`2` (default) — 3rd-tap-hold switch**: 2 taps + hold = scroll (middle hold sacrificed — keeps all three clicks and both useful drags)
    - **`3` — hold switch (browsing-first)**: plain hold = scroll; drags shift down a slot (tap+hold = left drag, 2 taps+hold = right drag). Paired with `sbtn 0`, a plain hold toggles modes in **both** directions
    - **`4` — fast tap (gaming)**: the button IS the left mouse button — zero-latency click and drag, no gestures and no on-device switch (use the GUI / serial)
  - In scroll mode the button only returns to mouse mode: `sbtn 0` (default) = hold, taps ignored · `sbtn 1` = tap, fires instantly on press
  - A "slow tap" in slots 1/2 resolves as a hold of the same button and releases immediately — still a plain click, so sloppy timing degrades gracefully.
- **Mode blink**: on boot and on every mode change, the RX LED blinks the new mode — 1× = joystick, 2× = mouse, 3× = scroll. (The USB core also pulses this LED on incoming serial data, so expect extra flashes while the GUI is connected.)
- **Auto Return** (`sidle`, default off): in scroll mode, after N seconds with no stick or button activity the device returns to mouse mode by itself. Note that long reading pauses count as inactivity — if you get kicked out mid-read, raise the value or set 0
- Speed uses the same quadratic curve as mouse mode — gentle deflection scrolls line-by-line, full deflection scrolls fast
- With **axis lock** on (default), diagonal deflection only scrolls the dominant axis, preventing accidental horizontal drift; turn it off (`slock 0`) for free two-axis panning
