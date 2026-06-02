/*
 * Jasp Keyboard Joystick - Enhanced Firmware
 *
 * Modes:
 *   - Joystick (default): HID gamepad with 2 axes + 1 button
 *   - Mouse: analog cursor control with quadratic curve + sub-pixel accumulator
 *   - Scroll: stick scrolls vertically/horizontally.
 *     Horizontal scroll (AC Pan) uses a custom HID report — the stock Mouse.h
 *     only exposes the vertical wheel.
 *
 * Button gestures — fully deferred: NOTHING is sent before a gesture
 * resolves, so taps can never leak clicks. Grammar (mouse mode):
 *   N quick taps                  = click of button N (1=L, 2=R, 3=M)
 *   (N-1) taps + press held      = HOLD of button N until release (drag)
 * `mbtn` picks where switching to scroll mode lives (or disables gestures):
 *   0 = mouse only   — full L/R/M clicks + holds, no on-device switch
 *   1 = 3-tap switch — 3 taps = scroll mode (middle CLICK sacrificed)
 *   2 = 3rd-tap-hold — 2 taps + hold = scroll mode (middle HOLD sacrificed,
 *       default: keeps all three clicks and both useful drags)
 *   3 = hold switch  — plain hold = scroll mode, drags shift down a slot
 *       (tap+hold = left drag, 2 taps+hold = right drag) — browsing-first:
 *       hold toggles modes both ways when paired with sbtn 0
 *   4 = fast tap     — the button IS the left button: zero-latency click and
 *       drag, no gestures, no on-device switch (gaming)
 * `sbtn` (scroll mode — the button only switches back):
 *   0 = hold to return to mouse (taps ignored, default)
 *   1 = tap to return (fires immediately on press)
 *
 * Configuration:
 *   Open a Web Serial terminal (or Arduino IDE Serial Monitor) at 115200 baud.
 *   Type `help` for the command list. All settings persist to EEPROM.
 *
 * Required libraries:
 *   - Joystick.h    (MHeironimus/ArduinoJoystickLibrary, install from ZIP)
 *   - Mouse.h       (bundled with Arduino IDE, Leonardo-compatible boards)
 *   - EEPROM.h      (bundled with Arduino IDE)
 */

#include <Joystick.h>
#include <Mouse.h>
#include <EEPROM.h>
#include <HID.h>

// ---------- Scroll HID (vertical wheel + AC Pan) ----------
// Separate top-level mouse collection so we can emit horizontal scroll, which
// the bundled Mouse.h doesn't support. Coexists with Mouse (report id 1),
// Keyboard (2) and the Joystick library (3).
//
// NOTE: Windows' mouse driver (mouhid.sys) refuses to start a Mouse-usage
// collection that lacks Button/X/Y usages, so this mirrors the full stock
// mouse layout (tilt-wheel style) and just always reports 0 for those fields.
static const uint8_t SCROLL_REPORT_ID = 0x0A;
static const uint8_t scrollHidDescriptor[] PROGMEM = {
  0x05, 0x01,                    // Usage Page (Generic Desktop)
  0x09, 0x02,                    // Usage (Mouse)
  0xA1, 0x01,                    // Collection (Application)
  0x85, SCROLL_REPORT_ID,        //   Report ID
  0x09, 0x01,                    //   Usage (Pointer)
  0xA1, 0x00,                    //   Collection (Physical)
  0x05, 0x09,                    //     Usage Page (Button) — dummy, always 0
  0x19, 0x01,                    //     Usage Minimum (1)
  0x29, 0x03,                    //     Usage Maximum (3)
  0x15, 0x00,                    //     Logical Minimum (0)
  0x25, 0x01,                    //     Logical Maximum (1)
  0x95, 0x03,                    //     Report Count (3)
  0x75, 0x01,                    //     Report Size (1)
  0x81, 0x02,                    //     Input (Data, Variable, Absolute)
  0x95, 0x01,                    //     Report Count (1)
  0x75, 0x05,                    //     Report Size (5)
  0x81, 0x03,                    //     Input (Constant) — bit padding
  0x05, 0x01,                    //     Usage Page (Generic Desktop)
  0x09, 0x30,                    //     Usage (X) — dummy, always 0
  0x09, 0x31,                    //     Usage (Y) — dummy, always 0
  0x09, 0x38,                    //     Usage (Wheel)
  0x15, 0x81,                    //     Logical Minimum (-127)
  0x25, 0x7F,                    //     Logical Maximum (127)
  0x75, 0x08,                    //     Report Size (8)
  0x95, 0x03,                    //     Report Count (3)
  0x81, 0x06,                    //     Input (Data, Variable, Relative)
  0x05, 0x0C,                    //     Usage Page (Consumer)
  0x0A, 0x38, 0x02,              //     Usage (AC Pan)
  0x15, 0x81,                    //     Logical Minimum (-127)
  0x25, 0x7F,                    //     Logical Maximum (127)
  0x75, 0x08,                    //     Report Size (8)
  0x95, 0x01,                    //     Report Count (1)
  0x81, 0x06,                    //     Input (Data, Variable, Relative)
  0xC0,                          //   End Collection
  0xC0                           // End Collection
};

class ScrollHID_ {
public:
  ScrollHID_() {
    static HIDSubDescriptor node(scrollHidDescriptor, sizeof(scrollHidDescriptor));
    HID().AppendDescriptor(&node);
  }
  // wheel: + = scroll up, pan: + = scroll right
  void send(int8_t wheel, int8_t pan) {
    // buttons, x, y are dummies required by mouhid — always 0
    uint8_t data[5] = { 0, 0, 0, (uint8_t)wheel, (uint8_t)pan };
    HID().SendReport(SCROLL_REPORT_ID, data, sizeof(data));
  }
};
ScrollHID_ ScrollWheel;

// ---------- Hardware ----------
const uint8_t X_PIN   = A0;
const uint8_t Y_PIN   = A1;
const uint8_t BTN_PIN = 14;
const uint8_t LED_PIN = 17;  // Pro Micro RX LED, active LOW (no user LED)

// ---------- Persistent config ----------
struct Config {
  uint16_t magic;
  int16_t  xCenter;
  int16_t  yCenter;
  int16_t  deadzone;     // ADC counts, snapped to center if |raw - center| < dz
  uint8_t  mode;         // 0 = joystick, 1 = mouse, 2 = scroll
  uint8_t  mouseSpeed;   // 1..30, scales the quadratic curve
  bool     invertX;
  bool     invertY;
  int16_t  gateX[8];     // 8-direction polar gate: offsets from center in raw ADC
  int16_t  gateY[8];     //   sampled by the GUI and written via `setgate`
  uint8_t  scrollSpeed;  // 1..30, scales the scroll rate
  bool     invertScroll; // flip scroll direction (natural scrolling)
  bool     scrollAxisLock; // true = only the dominant axis scrolls at a time
  uint16_t tapWindow;    // ms, multi-click gesture window (press-to-press)
  uint8_t  scrollBtnStyle; // scroll-mode button: 0 = hold returns to mouse
                           // (taps ignored), 1 = tap returns immediately
  uint16_t idleReturnSec;  // scroll mode: auto-return to mouse after N s with
                           // no stick/button activity (0 = off)
  uint8_t  mouseBtnStyle;  // which gesture slot switches to scroll mode:
                           //   0 = none (mouse only), 1 = 3 taps,
                           //   2 = 2 taps + hold (default), 3 = plain hold,
                           //   4 = fast tap (instant left button, no gestures)
};
const uint16_t CONFIG_MAGIC    = 0x4A5C;  // — deferred tap/hold gesture engine
const uint16_t CONFIG_MAGIC_V5 = 0x4A5B;  // old passthrough/gesture styles
const uint16_t CONFIG_MAGIC_V4 = 0x4A5A;  // idleReturnSec, shared button style
const uint16_t CONFIG_MAGIC_V3 = 0x4A59;  // scrollBtnStyle enum, no idle field
const uint16_t CONFIG_MAGIC_V2 = 0x4A58;  // bool scrollBtnGesture
const uint16_t CONFIG_MAGIC_V1 = 0x4A57;  // no scroll-button field
Config cfg;

// ---------- Runtime ----------
Joystick_ Joystick;
bool g_mouseDown = false;
uint8_t g_mouseBtn = MOUSE_LEFT;  // which mouse button is currently held
bool g_prevBtn = false;           // raw button state last loop (edge detection)
uint8_t g_seq = 0;                // presses in the current gesture sequence
unsigned long g_pressStart = 0;   // millis() of the latest press edge
unsigned long g_lastRelease = 0;  // millis() of the latest tap release —
                                  // the chaining window counts from here, so
                                  // time spent held never eats the tap budget

// Button debounce: tap counting is edge-sensitive, and contact bounce on a
// raw 8 ms sampling loop can mint phantom press edges that inflate the count
// (e.g. releasing the 2nd tap fires the 3-tap action). Accept a new state
// only after it has been stable for DEBOUNCE_MS.
bool g_btnRaw = false;            // last raw sample
bool g_btnStable = false;         // debounced state the rest of the code sees
unsigned long g_btnRawMs = 0;     // when the raw sample last changed
const uint8_t DEBOUNCE_MS = 20;
unsigned long g_lastActive = 0;   // last stick/button activity in scroll mode

uint8_t g_blinkLeft = 0;          // mode blink: remaining on/off half-phases
unsigned long g_blinkNextMs = 0;
const uint16_t BLINK_HALF_MS = 120;
bool g_stream    = false;
bool g_debug     = false;
unsigned long g_lastStream = 0;
float g_xAcc = 0, g_yAcc = 0;  // mouse sub-pixel accumulator

// Output of the polar-gate normalize step. Bundles intermediates so the
// `debug` stream can publish exactly what the runtime sees.
struct NormOut {
  float dx, dy;   // [-1, +1] after gate normalization (invert applied later)
  float theta;    // input angle in [0, 2π)
  float m;        // raw deflection magnitude (ADC counts)
  float r;        // interpolated gate radius at theta (ADC counts)
  float scale;    // m / r, clamped to 1
};

// ---------- Mode blink (non-blocking, RX LED) ----------
// Blink (mode+1) times on every mode change: 1 = joystick, 2 = mouse,
// 3 = scroll. The USB core also pulses this LED on CDC receive, so expect
// the occasional extra flash while the config GUI is talking.
void startModeBlink() {
  g_blinkLeft = (cfg.mode + 1) * 2;
  g_blinkNextMs = 0;  // fire on the next serviceBlink()
}

void serviceBlink() {
  if (!g_blinkLeft) return;
  unsigned long now = millis();
  if (g_blinkNextMs && now < g_blinkNextMs) return;
  g_blinkNextMs = now + BLINK_HALF_MS;
  digitalWrite(LED_PIN, (g_blinkLeft & 1) ? HIGH : LOW);  // even phase = on
  g_blinkLeft--;
}

// ---------- Config helpers ----------
void saveConfig() { EEPROM.put(0, cfg); }

void resetDefaults() {
  cfg.magic      = CONFIG_MAGIC;
  cfg.xCenter    = 512;
  cfg.yCenter    = 512;
  cfg.deadzone   = 15;
  cfg.mode       = 0;
  cfg.mouseSpeed = 10;
  // (button gesture defaults are set below with the scroll fields)
  cfg.invertX    = false;
  cfg.invertY    = false;
  cfg.scrollSpeed    = 10;
  cfg.invertScroll   = false;
  cfg.scrollAxisLock = true;
  cfg.tapWindow      = 250;
  cfg.scrollBtnStyle = 0;   // hold returns to mouse
  cfg.idleReturnSec  = 0;
  cfg.mouseBtnStyle  = 2;   // 2 taps + hold switches to scroll
  // Default gate: ~radius-400 octagon (placeholder until GUI sends a real cal)
  static const int16_t DEFX[8] = { 400,  283,    0, -283, -400, -283,    0,  283 };
  static const int16_t DEFY[8] = {   0,  283,  400,  283,    0, -283, -400, -283 };
  for (int i = 0; i < 8; i++) { cfg.gateX[i] = DEFX[i]; cfg.gateY[i] = DEFY[i]; }
  saveConfig();
}

// Gate radius at angle theta (in [0, 2π)): linear interp between adjacent
// stored vertices i=floor(theta/(π/4)) and i+1.
float gateRadius(float theta) {
  float sectorF = theta / (PI / 4.0f);
  int i = (int)sectorF;
  if (i < 0) i = 0; else if (i > 7) i = 7;
  int j = (i + 1) & 7;
  float frac = sectorF - i;
  float gx = (1.0f - frac) * cfg.gateX[i] + frac * cfg.gateX[j];
  float gy = (1.0f - frac) * cfg.gateY[i] + frac * cfg.gateY[j];
  return sqrtf(gx*gx + gy*gy);
}

// Normalize a raw center-offset (dxRaw, dyRaw) against the polar gate.
// Returns the unit-clamped (dx, dy) plus the intermediates the debug stream
// publishes — atan2/sqrt are computed once, not again outside.
NormOut normalize(int dxRaw, int dyRaw) {
  NormOut o = {0, 0, 0, 0, 0, 0};
  float fx = (float)dxRaw, fy = (float)dyRaw;
  o.m = sqrtf(fx*fx + fy*fy);
  if (o.m < 0.5f) return o;
  o.theta = atan2f(fy, fx);
  if (o.theta < 0) o.theta += 2.0f * PI;
  o.r = gateRadius(o.theta);
  if (o.r < 1.0f) return o;
  o.scale = o.m / o.r;
  if (o.scale > 1.0f) o.scale = 1.0f;
  o.dx = (fx / o.m) * o.scale;
  o.dy = (fy / o.m) * o.scale;
  return o;
}

void loadConfig() {
  EEPROM.get(0, cfg);
  // Stepwise migration — every layout change appended fields, so older data
  // is always a valid prefix and stored calibration survives upgrades.
  bool migrated = false;
  if (cfg.magic == CONFIG_MAGIC_V1) {        // → V2: add scroll-button field
    cfg.scrollBtnStyle = 0;
    cfg.magic = CONFIG_MAGIC_V2;
    migrated = true;
  }
  if (cfg.magic == CONFIG_MAGIC_V2) {        // → V3: bool became style enum
    if (cfg.scrollBtnStyle > 1) cfg.scrollBtnStyle = 0;       // garbage guard
    else if (cfg.scrollBtnStyle == 1) cfg.scrollBtnStyle = 2; // old "gestures" = B
    cfg.magic = CONFIG_MAGIC_V3;
    migrated = true;
  }
  if (cfg.magic == CONFIG_MAGIC_V3) {        // → V4: add idle auto-return
    cfg.idleReturnSec = 0;
    cfg.magic = CONFIG_MAGIC_V4;
    migrated = true;
  }
  if (cfg.magic == CONFIG_MAGIC_V4) {        // → V5: per-mode button styles
    // V4's scroll style 3 ("click only") also disabled the mouse-mode toggle.
    cfg.mouseBtnStyle = (cfg.scrollBtnStyle == 3) ? 3 : 0;
    cfg.magic = CONFIG_MAGIC_V5;
    migrated = true;
  }
  if (cfg.magic == CONFIG_MAGIC_V5) {        // → V6: deferred gesture engine —
    // style enums changed meaning entirely, so reset them (and the tap window
    // default) to the new defaults; calibration and tuning stay untouched.
    cfg.mouseBtnStyle  = 2;
    cfg.scrollBtnStyle = 0;
    cfg.tapWindow      = 250;
    cfg.magic = CONFIG_MAGIC;
    migrated = true;
  }
  if (cfg.magic != CONFIG_MAGIC) resetDefaults();
  else if (migrated) saveConfig();
}

void calibrate() {
  long sx = 0, sy = 0;
  for (int i = 0; i < 64; i++) {
    sx += analogRead(X_PIN);
    sy += analogRead(Y_PIN);
    delay(2);
  }
  cfg.xCenter = sx / 64;
  cfg.yCenter = sy / 64;
  saveConfig();
  Serial.print(F("CENTER=")); Serial.print(cfg.xCenter);
  Serial.print(F(","));       Serial.println(cfg.yCenter);
}

void printGate() {
  Serial.print(F("GATE="));
  for (int i = 0; i < 8; i++) {
    Serial.print(cfg.gateX[i]); Serial.print(',');
    Serial.print(cfg.gateY[i]);
    if (i < 7) Serial.print(';');
  }
  Serial.println();
}

// Parse `setgate x0,y0,x1,y1,...,x7,y7` — 16 comma-separated int16 offsets.
bool applySetGate(const String &arg) {
  int16_t vals[16];
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)arg.length() && idx < 16; i++) {
    if (i == (int)arg.length() || arg.charAt(i) == ',') {
      vals[idx++] = (int16_t)arg.substring(start, i).toInt();
      start = i + 1;
    }
  }
  if (idx != 16) return false;
  for (int k = 0; k < 8; k++) {
    cfg.gateX[k] = vals[k * 2];
    cfg.gateY[k] = vals[k * 2 + 1];
  }
  saveConfig();
  return true;
}

const __FlashStringHelper* modeName() {
  switch (cfg.mode) {
    case 1:  return F("mouse");
    case 2:  return F("scroll");
    default: return F("joystick");
  }
}

void printStatus() {
  Serial.print(F("MODE="));     Serial.println(modeName());
  Serial.print(F("CENTER="));   Serial.print(cfg.xCenter);
  Serial.print(F(","));         Serial.println(cfg.yCenter);
  Serial.print(F("DEADZONE=")); Serial.println(cfg.deadzone);
  Serial.print(F("MSPEED="));   Serial.println(cfg.mouseSpeed);
  Serial.print(F("INVX="));     Serial.println(cfg.invertX ? 1 : 0);
  Serial.print(F("INVY="));     Serial.println(cfg.invertY ? 1 : 0);
  Serial.print(F("SSPEED="));   Serial.println(cfg.scrollSpeed);
  Serial.print(F("SINV="));     Serial.println(cfg.invertScroll ? 1 : 0);
  Serial.print(F("SLOCK="));    Serial.println(cfg.scrollAxisLock ? 1 : 0);
  Serial.print(F("TAP="));      Serial.println(cfg.tapWindow);
  Serial.print(F("SBTN="));     Serial.println(cfg.scrollBtnStyle);
  Serial.print(F("MBTN="));     Serial.println(cfg.mouseBtnStyle);
  Serial.print(F("SIDLE="));    Serial.println(cfg.idleReturnSec);
  printGate();
}

void releaseAllOutputs() {
  Joystick.setButton(0, false);
  if (g_mouseDown) {
    Mouse.release(g_mouseBtn);
    g_mouseDown = false;
  }
  g_xAcc = g_yAcc = 0;  // don't carry sub-pixel/sub-detent residue across modes
  g_seq = 0;            // ...nor a half-finished tap gesture
}

// Switch modes from either the serial parser or a button gesture.
void setMode(uint8_t m) {
  cfg.mode = m;
  saveConfig();
  releaseAllOutputs();
  g_lastActive = millis();  // fresh idle window when entering scroll mode
  startModeBlink();
  Serial.print(F("MODE=")); Serial.println(modeName());
}

// ---------- Deferred tap/hold gesture engine ----------
// Nothing is sent before a gesture resolves, so taps can never leak.
//   N quick taps             → click of button N (1 = L, 2 = R, 3 = M)
//   (N-1) taps + press held  → HOLD of button N until release (drag)
// Chaining window counts from each tap's RELEASE (held time costs nothing);
// the hold tier kicks in after tapWindow + 200 ms of continuous press.
// Slot-3 taps resolve right at the 3rd release (no 4-tap gestures exist),
// slots 1/2 resolve one tapWindow after release.
// mbtn trades one slot for switching to scroll mode:
//   0 = none, 1 = 3 taps switch (middle click lost),
//   2 = 2 taps + hold switch (middle hold lost) — default.
// Robustness note: a "slow tap" in slots 1/2 resolves as a hold of the SAME
// button and releases immediately — i.e. still a plain click. Only a slow
// 3rd tap can differ (switch instead of middle click on mbtn 2).
const uint8_t BTN_BY_SLOT[3] = { MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE };

void handleMouseButton(bool btn) {
  unsigned long now = millis();

  if (cfg.mouseBtnStyle == 4) {
    // Fast tap: the button IS the left button — zero-latency click and drag,
    // no gestures, no on-device switching (use the GUI / serial).
    if (btn && !g_prevBtn) {
      if (g_debug) Serial.println(F("BTN fast-press"));
      Mouse.press(MOUSE_LEFT);
      g_mouseBtn = MOUSE_LEFT;
      g_mouseDown = true;
    }
    if (!btn && g_mouseDown) {
      Mouse.release(g_mouseBtn);
      g_mouseDown = false;
    }
    return;
  }

  unsigned long holdMs = (unsigned long)cfg.tapWindow + 200;

  if (btn && !g_prevBtn) {           // press edge — extends the sequence
    g_seq = (g_seq < 3) ? g_seq + 1 : 3;
    g_pressStart = now;
    if (g_debug) { Serial.print(F("TAP ")); Serial.println(g_seq); }
  }

  // Hold tier: a press kept down past the threshold resolves immediately
  if (btn && !g_mouseDown && g_seq > 0 && (now - g_pressStart) >= holdMs) {
    uint8_t slot = g_seq;
    g_seq = 0;
    bool isSwitch = (cfg.mouseBtnStyle == 2 && slot >= 3) ||  // 2 taps + hold
                    (cfg.mouseBtnStyle == 3 && slot == 1);    // plain hold
    if (isSwitch) {
      if (g_debug) Serial.println(F("BTN hold-switch"));
      setMode(2);
    } else {
      // mbtn 3 gave the plain hold to switching, so its drags shift down a
      // slot: tap+hold = left drag, 2 taps+hold = right drag.
      uint8_t idx = (cfg.mouseBtnStyle == 3) ? slot - 2 : slot - 1;
      g_mouseBtn = BTN_BY_SLOT[idx];
      if (g_debug) { Serial.print(F("BTN hold ")); Serial.println(idx + 1); }
      Mouse.press(g_mouseBtn);       // drag until release
      g_mouseDown = true;
    }
  }

  if (!btn && g_prevBtn) {           // release edge
    if (g_mouseDown) {
      Mouse.release(g_mouseBtn);     // end of a hold/drag
      g_mouseDown = false;
    } else if (g_seq >= 3) {         // slot 3 resolves at release
      g_seq = 0;
      if (cfg.mouseBtnStyle == 1) {
        if (g_debug) Serial.println(F("BTN 3tap-switch"));
        setMode(2);
      } else {
        if (g_debug) Serial.println(F("BTN click 3"));
        Mouse.click(MOUSE_MIDDLE);
      }
    } else {
      g_lastRelease = now;           // tap done — wait for chain or window
    }
  }

  // Window expired with the button up: resolve slot 1/2 clicks
  if (!btn && g_seq > 0 && (now - g_lastRelease) > cfg.tapWindow) {
    uint8_t slot = g_seq;
    g_seq = 0;
    if (g_debug) { Serial.print(F("BTN click ")); Serial.println(slot); }
    Mouse.click(BTN_BY_SLOT[slot - 1]);
  }
}

// Scroll mode: the button's only job is returning to mouse mode.
//   sbtn 0 — hold past the threshold switches; short taps are ignored
//   sbtn 1 — switching fires immediately on press (no competing gestures)
void handleScrollButton(bool btn) {
  unsigned long now = millis();
  if (cfg.scrollBtnStyle == 1) {
    if (btn && !g_prevBtn) setMode(1);
    return;
  }
  if (btn && !g_prevBtn) {
    g_seq = 1;
    g_pressStart = now;
  }
  if (btn && g_seq == 1 && (now - g_pressStart) >= (unsigned long)cfg.tapWindow + 200) {
    g_seq = 0;
    if (g_debug) Serial.println(F("BTN hold-switch"));
    setMode(1);
  }
  if (!btn) g_seq = 0;
}

// ---------- Serial command parser ----------
void processSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "help") {
    Serial.println(F("help status cal setgate<x0,y0,..x7,y7> mode<j|m|s> dz<n> sens<n> invx<0|1> invy<0|1> ssens<n> sinv<0|1> slock<0|1> sbtn<0|1> mbtn<0-4> sidle<s> tap<ms> stream<0|1> debug<0|1> reset"));
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "cal") {
    calibrate();
  } else if (cmd.startsWith("setgate ")) {
    if (applySetGate(cmd.substring(8))) { Serial.println(F("GATE_SET")); printGate(); }
    else Serial.println(F("ERR setgate needs 16 comma-separated ints"));
  } else if (cmd.startsWith("mode")) {
    char c = cmd.charAt(cmd.length() - 1);
    if      (c == 'm' || c == '1') setMode(1);
    else if (c == 's' || c == '2') setMode(2);
    else                           setMode(0);
  } else if (cmd.startsWith("dz")) {
    cfg.deadzone = constrain((int)cmd.substring(2).toInt(), 0, 200);
    saveConfig();
    Serial.print(F("DEADZONE=")); Serial.println(cfg.deadzone);
  } else if (cmd.startsWith("sens")) {
    cfg.mouseSpeed = constrain((int)cmd.substring(4).toInt(), 1, 30);
    saveConfig();
    Serial.print(F("MSPEED=")); Serial.println(cfg.mouseSpeed);
  } else if (cmd.startsWith("ssens")) {
    cfg.scrollSpeed = constrain((int)cmd.substring(5).toInt(), 1, 30);
    saveConfig();
    Serial.print(F("SSPEED=")); Serial.println(cfg.scrollSpeed);
  } else if (cmd.startsWith("sinv")) {
    cfg.invertScroll = cmd.substring(4).toInt() != 0;
    saveConfig();
    Serial.print(F("SINV=")); Serial.println(cfg.invertScroll ? 1 : 0);
  } else if (cmd.startsWith("slock")) {
    cfg.scrollAxisLock = cmd.substring(5).toInt() != 0;
    saveConfig();
    Serial.print(F("SLOCK=")); Serial.println(cfg.scrollAxisLock ? 1 : 0);
  } else if (cmd.startsWith("sidle")) {
    cfg.idleReturnSec = constrain((int)cmd.substring(5).toInt(), 0, 120);
    saveConfig();
    Serial.print(F("SIDLE=")); Serial.println(cfg.idleReturnSec);
  } else if (cmd.startsWith("sbtn")) {
    cfg.scrollBtnStyle = constrain((int)cmd.substring(4).toInt(), 0, 1);
    saveConfig();
    releaseAllOutputs();  // drop any half-finished gesture/click state
    Serial.print(F("SBTN=")); Serial.println(cfg.scrollBtnStyle);
  } else if (cmd.startsWith("mbtn")) {
    cfg.mouseBtnStyle = constrain((int)cmd.substring(4).toInt(), 0, 4);
    saveConfig();
    releaseAllOutputs();  // drop any half-finished gesture/click state
    Serial.print(F("MBTN=")); Serial.println(cfg.mouseBtnStyle);
  } else if (cmd.startsWith("tap")) {
    cfg.tapWindow = constrain((int)cmd.substring(3).toInt(), 150, 600);
    saveConfig();
    Serial.print(F("TAP=")); Serial.println(cfg.tapWindow);
  } else if (cmd.startsWith("invx")) {
    cfg.invertX = cmd.substring(4).toInt() != 0;
    saveConfig();
    Serial.print(F("INVX=")); Serial.println(cfg.invertX ? 1 : 0);
  } else if (cmd.startsWith("invy")) {
    cfg.invertY = cmd.substring(4).toInt() != 0;
    saveConfig();
    Serial.print(F("INVY=")); Serial.println(cfg.invertY ? 1 : 0);
  } else if (cmd.startsWith("stream")) {
    g_stream = cmd.substring(6).toInt() != 0;
    Serial.print(F("STREAM=")); Serial.println(g_stream ? 1 : 0);
  } else if (cmd.startsWith("debug")) {
    g_debug = cmd.substring(5).toInt() != 0;
    Serial.print(F("DEBUG=")); Serial.println(g_debug ? 1 : 0);
  } else if (cmd == "reset") {
    resetDefaults();
    Serial.println(F("RESET"));
    printStatus();
  } else {
    Serial.println(F("ERR unknown (try `help`)"));
  }
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // active LOW — start off
  loadConfig();

  Joystick.begin();
  Joystick.setXAxisRange(0, 1023);
  Joystick.setYAxisRange(0, 1023);
  Mouse.begin();
  startModeBlink();  // announce the boot mode
}

void loop() {
  processSerial();
  serviceBlink();

  int xRaw = analogRead(X_PIN);
  int yRaw = analogRead(Y_PIN);

  // Debounced button read — see g_btnStable declaration for why
  bool raw = digitalRead(BTN_PIN) == LOW;
  if (raw != g_btnRaw) { g_btnRaw = raw; g_btnRawMs = millis(); }
  if ((millis() - g_btnRawMs) >= DEBOUNCE_MS) g_btnStable = g_btnRaw;
  bool btn = g_btnStable;

  // Snap to center when inside deadzone
  int x = (abs(xRaw - cfg.xCenter) < cfg.deadzone) ? cfg.xCenter : xRaw;
  int y = (abs(yRaw - cfg.yCenter) < cfg.deadzone) ? cfg.yCenter : yRaw;

  // Polar gate normalization — produces (dx, dy) in [-1, +1] with magnitude ≤ 1.
  // Same logic runs in the GUI so the dot mirrors what the host receives.
  NormOut nr = normalize(x - cfg.xCenter, y - cfg.yCenter);
  float dx = nr.dx, dy = nr.dy;
  if (cfg.invertX) dx = -dx;
  if (cfg.invertY) dy = -dy;

  if (cfg.mode == 0) {
    // -- Joystick mode -- map [-1, +1] → [0, 1023]
    int outX = constrain(512 + (int)(dx * 511.0f), 0, 1023);
    int outY = constrain(512 + (int)(dy * 511.0f), 0, 1023);
    Joystick.setXAxis(outX);
    Joystick.setYAxis(outY);
    Joystick.setButton(0, btn);
  } else if (cfg.mode == 1) {
    // -- Mouse mode -- keep joystick HID centered to avoid stuck input
    Joystick.setXAxis(512);
    Joystick.setYAxis(512);
    Joystick.setButton(0, false);

    // Quadratic curve preserves sign (x * |x|) and gives fine control near center
    g_xAcc += dx * fabs(dx) * cfg.mouseSpeed;
    g_yAcc += dy * fabs(dy) * cfg.mouseSpeed;
    int mx = (int)g_xAcc;
    int my = (int)g_yAcc;
    g_xAcc -= mx;
    g_yAcc -= my;
    if (mx != 0 || my != 0) Mouse.move(mx, my, 0);

    handleMouseButton(btn);
  } else {
    // -- Scroll mode -- keep joystick HID centered to avoid stuck input
    Joystick.setXAxis(512);
    Joystick.setYAxis(512);
    Joystick.setButton(0, false);

    float sdx = dx, sdy = dy;
    // Axis lock: only the dominant axis scrolls, so diagonal deflection
    // doesn't cause accidental horizontal drift while reading.
    if (cfg.scrollAxisLock) {
      if (fabs(sdx) >= fabs(sdy)) sdy = 0;
      else                        sdx = 0;
    }
    const float dir = cfg.invertScroll ? -1.0f : 1.0f;
    // Same quadratic curve as mouse mode, scaled down ~100x: wheel units are
    // detents (≈3 lines each), so full deflection at speed 10 ≈ 12 detents/s.
    // Stick up (dy < 0) should scroll up (wheel +), hence the sign flip on Y.
    g_xAcc += sdx * fabs(sdx) * cfg.scrollSpeed * 0.01f * dir;
    g_yAcc -= sdy * fabs(sdy) * cfg.scrollSpeed * 0.01f * dir;
    int pan   = (int)g_xAcc;
    int wheel = (int)g_yAcc;
    g_xAcc -= pan;
    g_yAcc -= wheel;
    if (pan != 0 || wheel != 0) ScrollWheel.send((int8_t)wheel, (int8_t)pan);

    handleScrollButton(btn);

    // Idle auto-return: hop back to mouse mode after idleReturnSec with no
    // stick deflection or button activity (0 = disabled).
    if (dx != 0.0f || dy != 0.0f || btn || g_seq > 0) {
      g_lastActive = millis();
    } else if (cfg.idleReturnSec != 0 && cfg.mode == 2 &&
               (millis() - g_lastActive) > cfg.idleReturnSec * 1000UL) {
      setMode(1);
    }
  }
  g_prevBtn = btn;

  // Live telemetry for the web config UI
  if (g_stream && (millis() - g_lastStream) > 50) {
    Serial.print(F("DATA "));
    Serial.print(xRaw); Serial.print(',');
    Serial.print(yRaw); Serial.print(',');
    Serial.print(btn ? 1 : 0); Serial.print(',');
    Serial.println(cfg.mode);
    if (g_debug) {
      // DBG mirrors what normalize() produced this cycle: post-invert dx/dy,
      // input angle, raw magnitude, gate radius at that angle, and scale.
      Serial.print(F("DBG "));
      Serial.print(dx, 3); Serial.print(',');
      Serial.print(dy, 3); Serial.print(',');
      Serial.print(nr.theta * 180.0f / PI, 1); Serial.print(',');
      Serial.print(nr.m, 1); Serial.print(',');
      Serial.print(nr.r, 1); Serial.print(',');
      Serial.println(nr.scale, 3);
    }
    g_lastStream = millis();
  }

  delay(8);  // ~125 Hz polling
}
