/*
 * Jasp Keyboard Joystick - XInput Firmware (game build)
 *
 * Enumerates as a genuine Xbox 360 controller (XUSB), so games see native
 * XInput input — no Steam Input / x360ce needed.
 *   Stick  -> left analog stick
 *   Button -> A button
 *
 * Build requirements (NOT the regular Arduino AVR core):
 *   - Boards package "ArduinoXInput AVR Boards" (dmadison) — compile with
 *     FQBN  xinput:avr:leonardo
 *   - Library "XInput" (dmadison, Library Manager)
 *
 * IMPORTANT — this build has NO USB serial (Xbox controllers have no CDC):
 *   - The web config UI and serial commands DO NOT work on this firmware.
 *   - Uploading (this firmware, or going back to the main one) requires a
 *     manual bootloader entry: short RST to GND twice quickly, then start
 *     the upload within the 8-second bootloader window.
 *
 * Calibration: reads the SAME EEPROM block the main firmware writes
 * (center, deadzone, invert flags, 8-direction polar gate). Calibrate with
 * the main firmware + web config first; this build never writes EEPROM.
 */

#include <XInput.h>
#include <EEPROM.h>

// ---------- Hardware (same wiring as the main firmware) ----------
const uint8_t X_PIN   = A0;
const uint8_t Y_PIN   = A1;
const uint8_t BTN_PIN = 14;

// ---------- Persistent config ----------
// Byte-for-byte copy of the main firmware's Config struct. Only the fields
// up to gateY are used here; the rest exist so offsets stay identical.
struct Config {
  uint16_t magic;
  int16_t  xCenter;
  int16_t  yCenter;
  int16_t  deadzone;
  uint8_t  mode;            // unused here — this build is always a gamepad
  uint8_t  mouseSpeed;      // unused
  bool     invertX;
  bool     invertY;
  int16_t  gateX[8];
  int16_t  gateY[8];
  uint8_t  scrollSpeed;     // unused
  bool     invertScroll;    // unused
  bool     scrollAxisLock;  // unused
  uint16_t tapWindow;       // unused
  uint8_t  scrollBtnStyle;  // unused
  uint16_t idleReturnSec;   // unused
  uint8_t  mouseBtnStyle;   // unused
};
Config cfg;

// Every main-firmware layout since 0x4A57 shares the calibration prefix
// (center / deadzone / invert / gate), so any of these magics is usable.
const uint16_t MAGIC_MIN = 0x4A57;
const uint16_t MAGIC_MAX = 0x4A5C;

void loadConfig() {
  EEPROM.get(0, cfg);
  if (cfg.magic < MAGIC_MIN || cfg.magic > MAGIC_MAX) {
    // No stored calibration — RAM-only defaults. Never write EEPROM here:
    // that block belongs to the main firmware.
    cfg.xCenter  = 512;
    cfg.yCenter  = 512;
    cfg.deadzone = 15;
    cfg.invertX  = false;
    cfg.invertY  = false;
    static const int16_t DEFX[8] = { 400,  283,    0, -283, -400, -283,    0,  283 };
    static const int16_t DEFY[8] = {   0,  283,  400,  283,    0, -283, -400, -283 };
    for (int i = 0; i < 8; i++) { cfg.gateX[i] = DEFX[i]; cfg.gateY[i] = DEFY[i]; }
  }
}

// ---------- Polar gate normalization (copied from the main firmware) ----------
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

void normalize(int dxRaw, int dyRaw, float &dx, float &dy) {
  dx = dy = 0;
  float fx = (float)dxRaw, fy = (float)dyRaw;
  float m = sqrtf(fx*fx + fy*fy);
  if (m < 0.5f) return;
  float theta = atan2f(fy, fx);
  if (theta < 0) theta += 2.0f * PI;
  float r = gateRadius(theta);
  if (r < 1.0f) return;
  float scale = m / r;
  if (scale > 1.0f) scale = 1.0f;
  dx = (fx / m) * scale;
  dy = (fy / m) * scale;
}

// ---------- Button debounce (20 ms, same as the main firmware) ----------
bool g_btnRaw = false;
bool g_btnStable = false;
unsigned long g_btnRawMs = 0;
const uint8_t DEBOUNCE_MS = 20;

// ---------- Setup / Loop ----------
void setup() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  loadConfig();

  XInput.setJoystickRange(-1000, 1000);  // we feed dx/dy scaled to ±1000
  XInput.setAutoSend(true);
  XInput.begin();
}

void loop() {
  int xRaw = analogRead(X_PIN);
  int yRaw = analogRead(Y_PIN);

  bool raw = digitalRead(BTN_PIN) == LOW;
  if (raw != g_btnRaw) { g_btnRaw = raw; g_btnRawMs = millis(); }
  if ((millis() - g_btnRawMs) >= DEBOUNCE_MS) g_btnStable = g_btnRaw;

  int x = (abs(xRaw - cfg.xCenter) < cfg.deadzone) ? cfg.xCenter : xRaw;
  int y = (abs(yRaw - cfg.yCenter) < cfg.deadzone) ? cfg.yCenter : yRaw;

  float dx, dy;
  normalize(x - cfg.xCenter, y - cfg.yCenter, dx, dy);
  if (cfg.invertX) dx = -dx;
  if (cfg.invertY) dy = -dy;

  // XInput convention: stick up = positive Y, hence the sign flip — our dy
  // is positive-down like the HID/mouse modes.
  XInput.setJoystick(JOY_LEFT, (int)(dx * 1000.0f), (int)(-dy * 1000.0f));
  XInput.setButton(BUTTON_A, g_btnStable);

  delay(8);  // ~125 Hz, matches the main firmware
}
