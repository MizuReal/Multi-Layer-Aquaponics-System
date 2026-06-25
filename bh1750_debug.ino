// ════════════════════════════════════════════════════════════════
//  BH1750 I²C Diagnostic — GY-302 Module
//
//  Tests:  I²C bus scan, address detect, live lux readings.
//  GY-302 modules typically use address 0x5C (ADDR pin = HIGH).
//  Default BH1750 library uses 0x23 (ADDR pin = LOW/GND).
// ════════════════════════════════════════════════════════════════

#include <Wire.h>
#include <BH1750.h>

BH1750 lightMeter;

const int I2C_ADDR_DEFAULT = 0x23;   // ADDR = LOW / floating
const int I2C_ADDR_ALT     = 0x5C;   // ADDR = HIGH (common on GY-302)

// ════════════════════════════════════════════════════════════════
//  Setup
// ════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("======================================"));
  Serial.println(F("  BH1750 I²C Diagnostic"));
  Serial.println(F("======================================"));
  Serial.println(F("  SDA = GPIO21    SCL = GPIO22"));
  Serial.println(F("  Adapter: GY-302"));

  Wire.begin(21, 22);
  Wire.setClock(50000);   // 50 kHz — slow and reliable for I²C debug

  Serial.println(F("  ⚠ If 'other error' persists, add external 4.7kΩ pull-ups:"));
  Serial.println(F("    GPIO21 (SDA) → 4.7kΩ → 3.3V"));
  Serial.println(F("    GPIO22 (SCL) → 4.7kΩ → 3.3V"));

  // ── Phase 1: I²C bus scan ──────────────────────────────────
  Serial.println(F("──────────────────────────────────────"));
  Serial.println(F("[1] I²C Bus Scan ..."));
  scanI2C();

  // ── Phase 2: try both addresses ────────────────────────────
  Serial.println(F("──────────────────────────────────────"));
  Serial.println(F("[2] Testing BH1750 addresses ..."));
  testAddress(I2C_ADDR_DEFAULT, "0x23 (ADDR=GND / floating)");
  testAddress(I2C_ADDR_ALT,     "0x5C (ADDR=VCC, typical GY-302)");

  // Set CONTINUOUS mode for loop readings
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, I2C_ADDR_DEFAULT)) {
    Serial.println(F("  Loop init (CONTINUOUS) → OK ✓"));
  } else {
    Serial.println(F("  Loop init (CONTINUOUS) → FAIL ✗"));
  }

  Serial.println(F("──────────────────────────────────────"));
  Serial.println(F("  Lux reading every 1000 ms below:"));
  Serial.println(F("──────────────────────────────────────"));
}

// ════════════════════════════════════════════════════════════════
//  I²C Scanner
// ════════════════════════════════════════════════════════════════

void scanI2C() {
  int count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte err = Wire.endTransmission();
    if (err == 0) {
      Serial.print(F("    Found device at 0x"));
      Serial.print(addr, HEX);
      Serial.print(F(" ("));
      Serial.print(addr);
      Serial.print(F(")"));
      if (addr == I2C_ADDR_DEFAULT) Serial.print(F("  ← BH1750 default"));
      if (addr == I2C_ADDR_ALT)     Serial.print(F("  ← BH1750 GY-302"));
      Serial.println();
      count++;
    }
  }
  if (count == 0) {
    Serial.println(F("    No devices found."));
    Serial.println(F("    Check: power (3.3V), SDA/SCL wiring, pull-up resistors"));
  } else {
    Serial.print(F("    Total devices found: "));
    Serial.println(count);
  }
}

// ════════════════════════════════════════════════════════════════
//  Test a single I²C address
// ════════════════════════════════════════════════════════════════

void testAddress(byte addr, const char* label) {
  bool ok = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, addr);
  Serial.print(F("  begin("));
  Serial.print(label);
  Serial.print(F(") → "));
  Serial.println(ok ? F("OK ✓") : F("FAIL ✗"));

  if (ok) {
    // Wait for first measurement to complete (CONTINUOUS mode takes ~120ms)
    delay(200);
    float lux = lightMeter.readLightLevel();
    Serial.print(F("    First reading (CONTINUOUS mode): "));
    if (lux > -1.99f) {
      Serial.print(lux, 1);
      Serial.println(F(" lux"));
    } else {
      Serial.println(F("ERROR"));
      return;
    }

    // Also test ONE_TIME mode
    Serial.print(F("    Re-init in ONE_TIME mode ... "));
    ok = lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE, addr);
    Serial.println(ok ? F("OK ✓") : F("FAIL ✗"));
    if (ok) {
      delay(200);
      lux = lightMeter.readLightLevel();
      Serial.print(F("    One-time reading: "));
      if (lux > -1.99f) Serial.print(lux, 1); else Serial.print(F("ERR"));
      Serial.println(F(" lux"));
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  Loop
// ════════════════════════════════════════════════════════════════

void loop() {
  static unsigned long lastTime = 0;
  static int badReadings = 0;
  unsigned long now = millis();

  if (now - lastTime >= 1000) {
    lastTime = now;

    float lux = lightMeter.readLightLevel();
    Serial.print(F("  ["));
    Serial.print(now);
    Serial.print(F(" ms]  Lux: "));
    if (lux <= -1.99f) {
      badReadings++;
      Serial.print(F("-2 ✗"));
    } else if (lux < 0.0f) {
      badReadings++;
      Serial.print(lux, 1);
      Serial.print(F(" ⚠"));
    } else {
      Serial.print(lux, 1);
      Serial.print(F(" ✓"));
    }
    Serial.printf("  (errors: %d)  ", badReadings);
    if (badReadings > 5) {
      Serial.println(F("⚠ STOPPED — 5+ errors. Add 4.7kΩ pull-ups on SDA/SCL."));
      while (true) delay(1000);  // freeze — scroll up to see setup output
    } else {
      Serial.println();
    }
  }
}
