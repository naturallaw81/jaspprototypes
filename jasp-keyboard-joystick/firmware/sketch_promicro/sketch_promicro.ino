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
};
const uint16_t CONFIG_MAGIC = 0x4A53;  // 'JS'
Config cfg;

// ---------- Runtime ----------
Joystick_ Joystick;
bool g_mouseDown = false;
bool g_stream    = false;
unsigned long g_lastStream = 0;
float g_xAcc = 0, g_yAcc = 0;  // mouse sub-pixel accumulator

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
  saveConfig();
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

void printStatus() {
  Serial.print(F("MODE="));     Serial.println(cfg.mode == 0 ? F("joystick") : F("mouse"));
  Serial.print(F("CENTER="));   Serial.print(cfg.xCenter);
  Serial.print(F(","));         Serial.println(cfg.yCenter);
  Serial.print(F("DEADZONE=")); Serial.println(cfg.deadzone);
  Serial.print(F("MSPEED="));   Serial.println(cfg.mouseSpeed);
  Serial.print(F("INVX="));     Serial.println(cfg.invertX ? 1 : 0);
  Serial.print(F("INVY="));     Serial.println(cfg.invertY ? 1 : 0);
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
    Serial.println(F("help status cal mode<j|m> dz<n> sens<n> invx<0|1> invy<0|1> stream<0|1> reset"));
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "cal") {
    calibrate();
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

  if (cfg.mode == 0) {
    // -- Joystick mode --
    int outX = cfg.invertX ? (1023 - x) : x;
    int outY = cfg.invertY ? (1023 - y) : y;
    Joystick.setXAxis(outX);
    Joystick.setYAxis(outY);
    Joystick.setButton(0, btn);
  } else {
    // -- Mouse mode -- keep joystick HID centered to avoid stuck input
    Joystick.setXAxis(cfg.xCenter);
    Joystick.setYAxis(cfg.yCenter);
    Joystick.setButton(0, false);

    float dx = (float)(x - cfg.xCenter) / 512.0f;
    float dy = (float)(y - cfg.yCenter) / 512.0f;
    if (cfg.invertX) dx = -dx;
    if (cfg.invertY) dy = -dy;

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
    g_lastStream = millis();
  }

  delay(8);  // ~125 Hz polling
}
