#include <Wire.h>
#include <EEPROM.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Voltage Protector - ATmega328P / Simulino UNO
// A0: Voltage sense, A1: Current sense
// D2: Relay, D4: LED, D5: Buzzer
// D8: MENU, D9: UP, D10: DOWN, D11: RESET
// A4: SDA, A5: SCL

// LCD JHD-2X16-I2C, address 0x3E
class Lcd3E {
public:
  void cmd(uint8_t c) {
    Wire.beginTransmission(0x3E);
    Wire.write(0x80);
    Wire.write(c);
    Wire.endTransmission();
    delay(2);
  }

  void init() {
    Wire.begin();
    delay(100);
    cmd(0x38);
    cmd(0x39);
    cmd(0x14);
    cmd(0x7F);
    cmd(0x5E);
    cmd(0x6C);
    delay(300);
    cmd(0x38);
    cmd(0x0C);
    cmd(0x01);
    delay(5);
    cmd(0x06);
  }

  void backlight() {}

  void setCursor(uint8_t c, uint8_t r) {
    cmd(0x80 | (r ? 0x40 : 0) | c);
  }

  void print(const char* s) {
    Wire.beginTransmission(0x3E);
    Wire.write(0x40);
    while (*s) Wire.write(*s++);
    Wire.endTransmission();
  }
};

Lcd3E lcd;

// Pin definitions
const int RELAY = 2;
const int LED = 4;
const int BUZ = 5;

const int B_MENU = 8;
const int B_UP = 9;
const int B_DN = 10;
const int B_RST = 11;

// Sensor calibration (check: normal source should read about 230 V)
const float VREF = 5.0;
const float V_SCALE = 204.65;
const float I_SENS = 0.185;
const float I_ZERO = 2.5;

// First connect after power-up (independent of REC)
const unsigned long STARTUP_MS = 1000;
bool startup = true;

// Reconnect only when comfortably inside the limits
const int HYST_V = 5;

bool ocLatch = false;                       // over-current seen: needs manual RESET

// Configuration
struct Cfg {
  uint16_t magic;
  uint16_t ov;
  uint16_t uv;
  float oc;
  uint16_t dly;
  uint16_t rec;
} cfg;

float vrms = 0;
float iavg = 0;

bool tripped = true;
bool streamEnabled = true;

uint8_t cause = 0;
uint8_t fault = 0;
uint8_t menu = 0;

unsigned long faultSince = 0;
unsigned long tripTime = 0;
unsigned long lastTx = 0;
unsigned long lastBtnMs = 0;

char rx[32];
uint8_t rxLen = 0;
unsigned long lastRxMs = 0;
unsigned long holdUntil = 0;                // no DATA lines before this time

// Save settings
void saveCfg() {
  EEPROM.put(0, cfg);
}

// Restrict settings to valid ranges
void clampCfg() {
  cfg.ov = constrain(cfg.ov, 200, 400);
  cfg.uv = constrain(cfg.uv, 50, 220);
  cfg.oc = constrain(cfg.oc, 0.5, 20.0);
  cfg.dly = constrain(cfg.dly, 0, 5000);
  cfg.rec = constrain(cfg.rec, 0, 600);

  // keep OV at least 20 V above UV so the reconnect band always exists
  if (cfg.ov < cfg.uv + 20) {
    cfg.ov = min(400, (int)cfg.uv + 20);
  }
}

// Load settings from EEPROM
void loadCfg() {
  EEPROM.get(0, cfg);

  if (cfg.magic != 0xA55A) {
    cfg.magic = 0xA55A;
    cfg.ov = 250;
    cfg.uv = 180;
    cfg.oc = 10.0;
    cfg.dly = 100;
    cfg.rec = 5;
    saveCfg();
  }

  clampCfg();
  saveCfg();
}

// Measure AC voltage and current
void measure() {
  unsigned long n = 0;
  unsigned long s = 0;
  unsigned long s2 = 0;
  unsigned long t0 = millis();
  float isum = 0;

  while (millis() - t0 < 40) {
    int a = analogRead(A0);

    s += a;
    s2 += (unsigned long)a * a;

    float currentVoltage = analogRead(A1) * VREF / 1023.0;
    isum += fabs(currentVoltage - I_ZERO) / I_SENS;

    n++;
  }

  if (n == 0) return;

  float mean = (float)s / n;
  float var = (float)s2 / n - mean * mean;

  vrms = sqrt(var > 0 ? var : 0) * VREF / 1023.0 * V_SCALE;
  iavg = isum / n;
}

// Close the relay and clear the trip
void clearTrip() {
  tripped = false;
  startup = false;
  ocLatch = false;
  cause = 0;
  faultSince = 0;
  digitalWrite(RELAY, HIGH);
}

// Protection logic
void protect() {
  fault = (vrms > cfg.ov) ? 1 :
          (vrms < cfg.uv) ? 2 :
          (iavg > cfg.oc) ? 3 : 0;

  if (!tripped) {
    if (fault) {
      if (!faultSince) faultSince = millis();

      if (millis() - faultSince >= cfg.dly) {
        digitalWrite(RELAY, LOW);
        tripped = true;
        cause = fault;
        ocLatch = (iavg > cfg.oc);        // latch if over-current is present
        tripTime = millis();
        menu = 0;                         // show the TRIP screen immediately
      }
    } else {
      faultSince = 0;
    }
  } else {
    // While tripped, the shown cause follows the live fault
    if (fault) {
      cause = fault;
      tripTime = millis();                // reconnect timer restarts while a fault is present
    }
    if (iavg > cfg.oc) ocLatch = true;    // over-current stays latched until RESET

    // Power-up connect (1 s) or automatic recovery (REC seconds).
    // Reconnect only when inside the limits by HYST_V (no chatter).
    bool inBand = vrms < cfg.ov - HYST_V &&
                  vrms > cfg.uv + HYST_V &&
                  iavg <= cfg.oc;
    bool autoOk = inBand && !ocLatch && (startup || cfg.rec);
    unsigned long waitMs = startup ? STARTUP_MS : cfg.rec * 1000UL;

    if (autoOk && millis() - tripTime >= waitMs) {
      clearTrip();
    }
  }
}

// Buttons: MENU, UP, DOWN, RESET
void buttons() {
  static bool menuPrev = HIGH;
  static bool upPrev = HIGH;
  static bool dnPrev = HIGH;
  static bool rstPrev = HIGH;

  bool menuNow = digitalRead(B_MENU);
  bool upNow = digitalRead(B_UP);
  bool dnNow = digitalRead(B_DN);
  bool rstNow = digitalRead(B_RST);

  // idle for 15 s on a settings screen: save and return to the main screen
  if (!menuNow || !upNow || !dnNow || !rstNow) lastBtnMs = millis();
  if (menu != 0 && millis() - lastBtnMs > 15000) {
    menu = 0;
    clampCfg();
    saveCfg();
  }

  if (menuPrev == HIGH && menuNow == LOW) {
    menu = (menu + 1) % 6;

    if (menu == 0) {
      clampCfg();
      saveCfg();
    }
  }

  int direction = 0;

  if (upPrev == HIGH && upNow == LOW) {
    direction = 1;
  } else if (dnPrev == HIGH && dnNow == LOW) {
    direction = -1;
  }

  if (direction != 0) {
    switch (menu) {
      case 1: cfg.ov  = constrain((int)cfg.ov  + 5 * direction, 200, 400);  break;
      case 2: cfg.uv  = constrain((int)cfg.uv  + 5 * direction, 50, 220);   break;
      case 3: cfg.oc += 0.5 * direction;                                    break;
      case 4: cfg.dly = constrain((int)cfg.dly + 10 * direction, 0, 5000);  break;
      case 5: cfg.rec = constrain((int)cfg.rec + direction, 0, 600);        break;
    }

    clampCfg();
    saveCfg();
  }

  if (rstPrev == HIGH && rstNow == LOW && fault == 0) {
    clearTrip();
  }

  menuPrev = menuNow;
  upPrev = upNow;
  dnPrev = dnNow;
  rstPrev = rstNow;

  delay(20);
}

// One DATA line: voltage, current, state, trip cause
void sendData() {
  Serial.print(F("DATA,"));
  Serial.print((int)vrms);
  Serial.print(',');
  Serial.print(iavg, 1);
  Serial.print(',');
  Serial.print(tripped ? F("TRIP") : F("ON"));
  Serial.print(',');
  Serial.println(cause);
}

// Handle serial commands
void handleCmd(String s) {
  s.trim();
  while (s.indexOf("  ") >= 0) s.replace("  ", " ");   // collapse double spaces
  if (s == "STREAMOFF") s = "STREAM OFF";
  if (s == "STREAMON")  s = "STREAM ON";

  if (s == "GET") {
    Serial.print(F("CFG,"));
    Serial.print(cfg.ov);
    Serial.print(',');
    Serial.print(cfg.uv);
    Serial.print(',');
    Serial.print(cfg.oc);
    Serial.print(',');
    Serial.print(cfg.dly);
    Serial.print(',');
    Serial.println(cfg.rec);

  } else if (s == "STATUS") {
    sendData();

  } else if (s == "STREAM OFF") {
    streamEnabled = false;
    Serial.println(F("OK,STREAM_OFF"));

  } else if (s == "STREAM ON") {
    streamEnabled = true;
    lastTx = millis();
    Serial.println(F("OK,STREAM_ON"));

  } else if (s == "RESET") {
    if (!fault) {
      clearTrip();
      Serial.println(F("OK"));
    } else {
      Serial.println(F("ERR FAULT ACTIVE"));
    }

  } else if (s.startsWith("SET ")) {
    int sp = s.indexOf(' ', 4);

    if (sp < 0) {
      Serial.println(F("ERR FORMAT"));
      return;
    }

    String k = s.substring(4, sp);
    float v = s.substring(sp + 1).toFloat();
    if (v < 0) v = 0;                     // no negative values (unsigned settings)

    if (k == "OV") {
      cfg.ov = (uint16_t)v;
    } else if (k == "UV") {
      cfg.uv = (uint16_t)v;
    } else if (k == "OC") {
      cfg.oc = v;
    } else if (k == "DLY") {
      cfg.dly = (uint16_t)min(v, 5000.0f);
    } else if (k == "REC") {
      cfg.rec = (uint16_t)min(v, 600.0f);
    } else {
      Serial.println(F("ERR KEY"));
      return;
    }

    clampCfg();
    saveCfg();
    Serial.println(F("OK"));

  } else {
    Serial.print(F("ERR CMD ["));
    Serial.print(s);
    Serial.println(F("]"));
  }
}

// Read serial commands. Any typed character pauses the stream (telemetry
// checks rxLen). 2 s after Enter the stream resumes, even after a wrong command.
void serialCmd() {
  // abandon a half-typed command after 10 s so the stream can resume
  if (rxLen > 0 && millis() - lastRxMs > 10000) rxLen = 0;

  while (Serial.available() > 0) {
    char c = Serial.read();
    lastRxMs = millis();

    if (c == '\r' || c == '\n') {
      if (rxLen > 0) {
        rx[rxLen] = '\0';
        handleCmd(String(rx));
        rxLen = 0;
        holdUntil = millis() + 2000;      // stream resumes 2 s after the command
      }
    } else if (c == 8 || c == 127) {      // backspace / delete
      if (rxLen > 0) rxLen--;
    } else if (c >= 32 && c <= 126 && rxLen < sizeof(rx) - 1) {
      rx[rxLen++] = toupper((unsigned char)c);
    }
  }
}

// LCD display: redraws at most 4 times a second, only changed lines
void display() {
  char a[17] = "", b[17] = "";

  const char* nm[] = {"", "OV", "UV", "OC", "DLY", "REC"};
  const char* cs[] = {"", "OV", "UV", "OC"};

  if (menu == 0) {
    int current10 = (int)(iavg * 10.0 + 0.5);

    snprintf(a, sizeof(a), "V:%3dV I:%2d.%dA",
             (int)vrms,
             current10 / 10,
             current10 % 10);

    snprintf(b, sizeof(b), "%s %s",
             tripped ? "TRIP" : "ON",
             tripped ? cs[cause] : "");
  } else {
    snprintf(a, sizeof(a), "SET %s", nm[menu]);

    switch (menu) {
      case 1:
        snprintf(b, sizeof(b), "%u V", cfg.ov);
        break;
      case 2:
        snprintf(b, sizeof(b), "%u V", cfg.uv);
        break;
      case 3: {
        int oc10 = (int)(cfg.oc * 10.0 + 0.5);
        snprintf(b, sizeof(b), "%d.%d A", oc10 / 10, oc10 % 10);
        break;
      }
      case 4:
        snprintf(b, sizeof(b), "%u ms", cfg.dly);
        break;
      case 5:
        snprintf(b, sizeof(b), "%u sec", cfg.rec);
        break;
    }
  }

  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "%-16.16s", a);
  snprintf(line2, sizeof(line2), "%-16.16s", b);

  static char last[2][17] = {"", ""};
  static unsigned long lastLcd = 0;

  if (millis() - lastLcd >= 250) {
    lastLcd = millis();

    if (strcmp(line1, last[0]) != 0) {
      lcd.setCursor(0, 0);
      lcd.print(line1);
      strcpy(last[0], line1);
    }
    if (strcmp(line2, last[1]) != 0) {
      lcd.setCursor(0, 1);
      lcd.print(line2);
      strcpy(last[1], line2);
    }
  }

  bool alarm = tripped && cause != 0;
  digitalWrite(LED, alarm);
  digitalWrite(BUZ, alarm);
}

// Telemetry every 500 ms while enabled, paused during typing and 2 s after Enter
void telemetry() {
  if (!streamEnabled || rxLen > 0 || millis() < holdUntil) return;
  if (millis() - lastTx < 500) return;

  lastTx = millis();
  sendData();
}

void setup() {
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BUZ, OUTPUT);

  pinMode(B_MENU, INPUT_PULLUP);
  pinMode(B_UP, INPUT_PULLUP);
  pinMode(B_DN, INPUT_PULLUP);
  pinMode(B_RST, INPUT_PULLUP);

  // Start disconnected
  digitalWrite(RELAY, LOW);
  digitalWrite(LED, LOW);
  digitalWrite(BUZ, LOW);

  Serial.begin(9600);

  lcd.init();
  lcd.backlight();

  loadCfg();

  tripTime = millis();
  lastTx = millis();
}

void loop() {
  serialCmd();

  measure();
  protect();
  buttons();
  display();
  telemetry();
}