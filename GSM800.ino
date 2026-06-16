//GSM800. Change the GSM 900a in the compiation.ino
// ============================================================
//  SIM800L – ESP32 Full Debug Sketch
//  Pin Config:  ESP32 GPIO17 (TX) → SIM800L RX
//               ESP32 GPIO16 (RX) ← SIM800L TX
//  VCC:         3.7–4.2V (NOT 3.3V, NOT 5V) — use LiPo or LM2596
//  Debug baud:  115200 (Serial Monitor)
//  SIM baud:    9600   (auto-detected)
// ============================================================

#include <HardwareSerial.h>

// ── Pin definitions ──────────────────────────────────────────
#define SIM_RX_PIN  16
#define SIM_TX_PIN  17
#define SIM_BAUD    9600

// ── UART instance ────────────────────────────────────────────
HardwareSerial simSerial(2);

// ── Forward declarations ─────────────────────────────────────
bool   sendAT(const char* cmd, const char* expected, uint32_t timeout_ms);
String sendATRaw(const char* cmd, uint32_t timeout_ms);
bool   autoBaud();
bool   checkSIMCard();
bool   checkNetwork();
bool   checkBattery();
bool   sendSMS(const char* number, const char* message);
void   flushSIM();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("\n========================================"));
  Serial.println(F("  SIM800L Debug Monitor - ESP32"));
  Serial.println(F("========================================"));
  Serial.printf("  SIM RX pin  : GPIO%d  (<- SIM800L TX)\n", SIM_RX_PIN);
  Serial.printf("  SIM TX pin  : GPIO%d  (-> SIM800L RX)\n", SIM_TX_PIN);
  Serial.printf("  SIM baud    : %d (default, autobaud will override)\n", SIM_BAUD);
  Serial.println(F("  VCC         : must be 3.7-4.2V @ 2A"));
  Serial.println(F("  GND         : shared with ESP32"));
  Serial.println(F("----------------------------------------"));

  simSerial.begin(SIM_BAUD, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  Serial.println(F("[INIT] HardwareSerial UART2 started"));

  // SIM800L needs 3-5s to fully boot after power-on
  Serial.println(F("[INIT] Waiting 5s for SIM800L boot..."));
  for (int i = 5; i > 0; i--) {
    Serial.printf("[INIT] %d...\n", i);
    delay(1000);
  }

  // ── Step 1: autobaud scan ─────────────────────────────────
  Serial.println(F("\n[BAUD] Scanning baud rates..."));
  if (!autoBaud()) {
    Serial.println(F("\n[FATAL] No response on any baud rate."));
    Serial.println(F("[FATAL] Possible causes:"));
    Serial.println(F("  1. RX/TX swapped   -- swap GPIO16 and GPIO17"));
    Serial.println(F("  2. VCC too low     -- SIM800L needs 3.7-4.2V, not 3.3V or 5V"));
    Serial.println(F("  3. Power too weak  -- needs 2A burst, USB alone won't work"));
    Serial.println(F("  4. Module not on   -- check red power LED (slow blink = no network)"));
    Serial.println(F("  5. No antenna      -- module may refuse to init without one"));
    Serial.println(F("[FATAL] Halting. Fix issue then reset ESP32."));
    while (true) delay(1000);
  }

  // ── Step 2: disable echo ──────────────────────────────────
  Serial.println(F("\n[CFG] Disabling echo (ATE0)..."));
  if (sendAT("ATE0", "OK", 2000))
    Serial.println(F("[CFG] OK Echo disabled"));
  else
    Serial.println(F("[CFG] FAIL ATE0 failed (non-fatal)"));

  // ── Step 3: verbose error codes ───────────────────────────
  Serial.println(F("\n[CFG] Enabling verbose errors (AT+CMEE=2)..."));
  if (sendAT("AT+CMEE=2", "OK", 2000))
    Serial.println(F("[CFG] OK Verbose errors enabled"));
  else
    Serial.println(F("[CFG] FAIL CMEE=2 failed (non-fatal)"));

  // ── Step 4: check battery / VCC ───────────────────────────
  Serial.println(F("\n[PWR] Checking supply voltage (AT+CBC)..."));
  checkBattery();

  // ── Step 5: check SIM card ────────────────────────────────
  Serial.println(F("\n[SIM] Checking SIM card (AT+CPIN?)..."));
  if (!checkSIMCard()) {
    Serial.println(F("[SIM] WARNING: SIM not ready, SMS will fail"));
  }

  // ── Step 6: check network with retries ───────────────────
  Serial.println(F("\n[NET] Checking network registration..."));
  Serial.println(F("[NET] Waiting up to 30s for network..."));
  bool netOk = false;
  for (int i = 0; i < 6; i++) {
    if (checkNetwork()) { netOk = true; break; }
    Serial.printf("[NET] Retry %d/6 in 5s...\n", i + 1);
    delay(5000);
  }
  if (!netOk) {
    Serial.println(F("[NET] WARNING: Not registered after 30s"));
    Serial.println(F("[NET]   Check: antenna, SIM plan active, GSM 2G coverage"));
    Serial.println(F("[NET]   Note: SIM800L is 2G only — won't work on VoLTE-only networks"));
  }

  // ── Step 7: signal quality ────────────────────────────────
  Serial.println(F("\n[NET] Signal quality (AT+CSQ)..."));
  String csq = sendATRaw("AT+CSQ", 3000);
  csq.trim();
  Serial.print(F("[NET] Raw: "));
  Serial.println(csq);
  int csqIdx = csq.indexOf("+CSQ:");
  if (csqIdx != -1) {
    String vals = csq.substring(csqIdx + 5);
    vals.trim();
    int rssi = vals.toInt();
    Serial.print(F("[NET] RSSI: "));
    Serial.print(rssi);
    if      (rssi == 0)  Serial.println(F(" -> NO SIGNAL"));
    else if (rssi == 99) Serial.println(F(" -> UNKNOWN / not detectable"));
    else if (rssi >= 20) Serial.println(F(" -> STRONG"));
    else if (rssi >= 10) Serial.println(F(" -> FAIR"));
    else                 Serial.println(F(" -> WEAK (may fail to send SMS)"));
  }

  // ── Step 8: operator name ─────────────────────────────────
  Serial.println(F("\n[NET] Checking operator (AT+COPS?)..."));
  String cops = sendATRaw("AT+COPS?", 5000);
  cops.trim();
  Serial.print(F("[NET] Operator: "));
  Serial.println(cops);

  // ── Step 9: module info ───────────────────────────────────
  Serial.println(F("\n[INFO] Module firmware (AT+GMR)..."));
  String gmr = sendATRaw("AT+GMR", 3000);
  gmr.trim();
  Serial.print(F("[INFO] Firmware: "));
  Serial.println(gmr);

  Serial.println(F("\n[INIT] Setup complete. Entering loop...\n"));
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Send SMS once on first loop tick
  static bool smsSent = false;
  if (!smsSent) {
    smsSent = true;
    Serial.println(F("\n[SMS] Sending test SMS..."));
    sendSMS("+639XXXXXXXXX", "Aquaponics alert: SIM800L OK");
  }

  // Echo anything the module sends unprompted (URCs, incoming calls, etc.)
  while (simSerial.available()) {
    char c = simSerial.read();
    Serial.print(c);
  }
}

// ============================================================
//  AUTOBAUD
//  SIM800L locks baud after receiving AT 3x — send blind first
// ============================================================
bool autoBaud() {
  const uint32_t bauds[] = {9600, 19200, 38400, 57600, 115200};
  const uint8_t  n       = sizeof(bauds) / sizeof(bauds[0]);

  for (uint8_t i = 0; i < n; i++) {
    simSerial.begin(bauds[i], SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
    delay(150);
    flushSIM();

    // Autobaud sync: 3 blind AT pings
    Serial.printf("[BAUD] Trying %lu baud (sync x3)...\n", bauds[i]);
    simSerial.println("AT"); delay(300);
    simSerial.println("AT"); delay(300);
    simSerial.println("AT"); delay(300);
    flushSIM();  // discard sync garbage

    // Real probe
    simSerial.println("AT");
    delay(600);

    String r;
    while (simSerial.available()) r += (char)simSerial.read();
    r.trim();

    // Hex dump for diagnosis
    Serial.printf("[BAUD] %6lu -> \"%s\" (hex:", bauds[i], r.c_str());
    for (size_t j = 0; j < r.length() && j < 16; j++) {
      Serial.printf(" %02X", (uint8_t)r[j]);
    }
    Serial.println(F(")"));

    if (r.indexOf("OK") != -1 || r.indexOf("AT") != -1) {
      Serial.printf("[BAUD] ✓ Working baud: %lu\n", bauds[i]);
      return true;
    }
  }

  simSerial.begin(SIM_BAUD, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  return false;
}

// ============================================================
//  CHECK BATTERY / VCC via AT+CBC
//  SIM800L: +CBC: <bcs>,<bcl>,<voltage_mV>
//  voltage should be 3500-4200mV; below 3400 = brownout risk
// ============================================================
bool checkBattery() {
  String r = sendATRaw("AT+CBC", 3000);
  r.trim();
  Serial.print(F("[PWR] Response: "));
  Serial.println(r);

  int idx = r.indexOf("+CBC:");
  if (idx == -1) {
    Serial.println(F("[PWR] FAIL Could not read voltage"));
    return false;
  }

  // Parse: +CBC: 0,100,4012  → third field is mV
  String fields = r.substring(idx + 5);
  fields.trim();
  int c1 = fields.indexOf(',');
  int c2 = fields.indexOf(',', c1 + 1);
  if (c1 == -1 || c2 == -1) {
    Serial.println(F("[PWR] FAIL Could not parse +CBC fields"));
    return false;
  }
  int voltage = fields.substring(c2 + 1).toInt();
  Serial.printf("[PWR] Supply voltage: %d mV\n", voltage);

  if      (voltage >= 3700) Serial.println(F("[PWR] OK Voltage nominal"));
  else if (voltage >= 3500) Serial.println(F("[PWR] WARN Voltage low — may cause resets under load"));
  else                      Serial.println(F("[PWR] CRIT Voltage critical — module will brownout"));

  return (voltage >= 3500);
}

// ============================================================
//  CHECK SIM CARD
// ============================================================
bool checkSIMCard() {
  String r = sendATRaw("AT+CPIN?", 5000);
  r.trim();
  Serial.print(F("[SIM] Response: "));
  Serial.println(r);

  if      (r.indexOf("READY")   != -1) { Serial.println(F("[SIM] OK SIM ready"));                   return true;  }
  else if (r.indexOf("SIM PIN") != -1) { Serial.println(F("[SIM] FAIL SIM locked - PIN required")); return false; }
  else if (r.indexOf("SIM PUK") != -1) { Serial.println(F("[SIM] FAIL SIM locked - PUK required")); return false; }
  else                                  { Serial.println(F("[SIM] FAIL No SIM or unreadable"));      return false; }
}

// ============================================================
//  CHECK NETWORK REGISTRATION
// ============================================================
bool checkNetwork() {
  String r = sendATRaw("AT+CREG?", 5000);
  r.trim();
  Serial.print(F("[NET] Response: "));
  Serial.println(r);

  if      (r.indexOf(",1") != -1) { Serial.println(F("[NET] OK Registered (home)"));      return true;  }
  else if (r.indexOf(",5") != -1) { Serial.println(F("[NET] OK Registered (roaming)"));   return true;  }
  else if (r.indexOf(",2") != -1) { Serial.println(F("[NET] ... Searching for network"));  return false; }
  else                             { Serial.println(F("[NET] FAIL Not registered"));       return false; }
}

// ============================================================
//  SEND SMS
// ============================================================
bool sendSMS(const char* number, const char* message) {
  Serial.printf("[SMS] To : %s\n", number);
  Serial.printf("[SMS] Msg: %s\n", message);

  // Text mode
  Serial.println(F("[SMS] Setting text mode (AT+CMGF=1)..."));
  if (!sendAT("AT+CMGF=1", "OK", 3000)) {
    Serial.println(F("[SMS] FAIL Could not set text mode"));
    return false;
  }

  // GSM charset
  sendAT("AT+CSCS=\"GSM\"", "OK", 2000);

  // CMGS header
  flushSIM();
  Serial.printf("[TX] AT+CMGS=\"%s\"\n", number);
  simSerial.print(F("AT+CMGS=\""));
  simSerial.print(number);
  simSerial.println(F("\""));

  // Wait for '>' prompt
  Serial.println(F("[SMS] Waiting for '>' prompt..."));
  uint32_t start = millis();
  bool gotPrompt = false;
  String promptBuf;
  while (millis() - start < 8000) {
    while (simSerial.available()) {
      char c = simSerial.read();
      promptBuf += c;
      if (c == '>') { gotPrompt = true; break; }
    }
    if (gotPrompt) break;
    if (promptBuf.indexOf("ERROR") != -1) {
      Serial.printf("[SMS] FAIL Error before prompt: \"%s\"\n", promptBuf.c_str());
      return false;
    }
    delay(10);
  }
  if (!gotPrompt) {
    Serial.printf("[SMS] FAIL No '>' prompt. Got: \"%s\"\n", promptBuf.c_str());
    return false;
  }
  Serial.println(F("[SMS] OK Got '>' prompt"));

  // Body + Ctrl-Z
  delay(100);
  simSerial.print(message);
  simSerial.write(0x1A);
  Serial.println(F("[SMS] Body + Ctrl-Z sent"));

  // Wait for +CMGS confirmation
  Serial.println(F("[SMS] Waiting for +CMGS confirmation (up to 15s)..."));
  start = millis();
  String resp;
  while (millis() - start < 15000) {
    while (simSerial.available()) resp += (char)simSerial.read();
    if (resp.indexOf("+CMGS:") != -1) {
      resp.trim();
      Serial.println(F("[SMS] OK SMS sent successfully"));
      Serial.printf("[SMS] Confirmation: %s\n", resp.c_str());
      return true;
    }
    if (resp.indexOf("ERROR") != -1) {
      resp.trim();
      Serial.printf("[SMS] FAIL: %s\n", resp.c_str());
      return false;
    }
    delay(10);
  }
  Serial.println(F("[SMS] FAIL Timeout waiting for +CMGS"));
  return false;
}

// ============================================================
//  SEND AT COMMAND — expects substring in response
// ============================================================
bool sendAT(const char* cmd, const char* expected, uint32_t timeout_ms) {
  flushSIM();
  Serial.printf("[TX] %s\n", cmd);
  simSerial.println(cmd);

  uint32_t start = millis();
  String resp;
  while (millis() - start < timeout_ms) {
    while (simSerial.available()) resp += (char)simSerial.read();
    if (resp.indexOf(expected) != -1) {
      resp.trim();
      Serial.printf("[RX] %s\n", resp.c_str());
      return true;
    }
    if (resp.indexOf("ERROR") != -1) {
      resp.trim();
      Serial.printf("[RX] ERROR: %s\n", resp.c_str());
      return false;
    }
    delay(10);
  }
  resp.trim();
  Serial.printf("[RX] TIMEOUT – got: \"%s\"\n", resp.c_str());
  return false;
}

// ============================================================
//  SEND AT COMMAND — returns full raw response
// ============================================================
String sendATRaw(const char* cmd, uint32_t timeout_ms) {
  flushSIM();
  Serial.printf("[TX] %s\n", cmd);
  simSerial.println(cmd);

  uint32_t start = millis();
  String resp;
  while (millis() - start < timeout_ms) {
    while (simSerial.available()) resp += (char)simSerial.read();
    if (resp.indexOf("OK") != -1 || resp.indexOf("ERROR") != -1) break;
    delay(10);
  }
  return resp;
}

// ============================================================
//  FLUSH SIM UART BUFFER
// ============================================================
void flushSIM() {
  while (simSerial.available()) simSerial.read();
}