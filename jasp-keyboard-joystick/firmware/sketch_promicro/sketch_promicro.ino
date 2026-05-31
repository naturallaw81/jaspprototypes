/*
 * Jasp Keyboard Joystick - Enhanced Firmware
 *
 * Modes:
 *   - Joystick (default): HID gamepad with 2 axes + 1 button
 *   - Mouse: analog cursor control with quadratic curve + sub-pixel accumulator
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

// ---------- Hardware ----------
const uint8_t X_PIN   = A0;
const uint8_t Y_PIN   = A1;
const uint8_t BTN_PIN = 14;

// ---------- Persistent config ----------
struct Config {
  uint16_t magic;
  int16_t  xCenter;
  int16_t  yCenter;
  int16_t  deadzone;     // ADC counts, snapped to center if |raw - center| < dz
  uint8_t  mode;         // 0 = joystick, 1 = mouse
  uint8_t  mouseSpeed;   // 1..30, scales the quadratic curve
  bool     invertX;
  bool     invertY;
  int16_t  gateX[8];     // 8-direction polar gate: offsets from center in raw ADC
  int16_t  gateY[8];     //   sampled by the GUI and written via `setgate`
};
const uint16_t CONFIG_MAGIC = 0x4A55;  // 'JU' — bumped for gate-based normalization
Config cfg;

// ---------- Runtime ----------
Joystick_ Joystick;
bool g_mouseDown = false;
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

// ---------- Config helpers ----------
void saveConfig() { EEPROM.put(0, cfg); }

void resetDefaults() {
  cfg.magic      = CONFIG_MAGIC;
  cfg.xCenter    = 512;
  cfg.yCenter    = 512;
  cfg.deadzone   = 15;
  cfg.mode       = 0;
  cfg.mouseSpeed = 10;
  cfg.invertX    = false;
  cfg.invertY    = false;
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
  if (cfg.magic != CONFIG_MAGIC) resetDefaults();
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

void printStatus() {
  Serial.print(F("MODE="));     Serial.println(cfg.mode == 0 ? F("joystick") : F("mouse"));
  Serial.print(F("CENTER="));   Serial.print(cfg.xCenter);
  Serial.print(F(","));         Serial.println(cfg.yCenter);
  Serial.print(F("DEADZONE=")); Serial.println(cfg.deadzone);
  Serial.print(F("MSPEED="));   Serial.println(cfg.mouseSpeed);
  Serial.print(F("INVX="));     Serial.println(cfg.invertX ? 1 : 0);
  Serial.print(F("INVY="));     Serial.println(cfg.invertY ? 1 : 0);
  printGate();
}

void releaseAllOutputs() {
  Joystick.setButton(0, false);
  if (g_mouseDown) {
    Mouse.release(MOUSE_LEFT);
    g_mouseDown = false;
  }
}

// ---------- Serial command parser ----------
void processSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "help") {
    Serial.println(F("help status cal setgate<x0,y0,..x7,y7> mode<j|m> dz<n> sens<n> invx<0|1> invy<0|1> stream<0|1> debug<0|1> reset"));
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "cal") {
    calibrate();
  } else if (cmd.startsWith("setgate ")) {
    if (applySetGate(cmd.substring(8))) { Serial.println(F("GATE_SET")); printGate(); }
    else Serial.println(F("ERR setgate needs 16 comma-separated ints"));
  } else if (cmd.startsWith("mode")) {
    char c = cmd.charAt(cmd.length() - 1);
    cfg.mode = (c == 'm' || c == '1') ? 1 : 0;
    saveConfig();
    releaseAllOutputs();
    Serial.print(F("MODE=")); Serial.println(cfg.mode == 0 ? F("joystick") : F("mouse"));
  } else if (cmd.startsWith("dz")) {
    cfg.deadzone = constrain((int)cmd.substring(2).toInt(), 0, 200);
    saveConfig();
    Serial.print(F("DEADZONE=")); Serial.println(cfg.deadzone);
  } else if (cmd.startsWith("sens")) {
    cfg.mouseSpeed = constrain((int)cmd.substring(4).toInt(), 1, 30);
    saveConfig();
    Serial.print(F("MSPEED=")); Serial.println(cfg.mouseSpeed);
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
  loadConfig();

  Joystick.begin();
  Joystick.setXAxisRange(0, 1023);
  Joystick.setYAxisRange(0, 1023);
  Mouse.begin();
}

void loop() {
  processSerial();

  int xRaw = analogRead(X_PIN);
  int yRaw = analogRead(Y_PIN);
  bool btn = digitalRead(BTN_PIN) == LOW;

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
  } else {
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

    // Edge-triggered click so we don't flood USB
    if (btn != g_mouseDown) {
      if (btn) Mouse.press(MOUSE_LEFT);
      else     Mouse.release(MOUSE_LEFT);
      g_mouseDown = btn;
    }
  }

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
