// ════════════════════════════════════════════════════════════════
//  pH Probe — Standalone Debug Sketch
//
//  Voltage divider: pH AO → 1kΩ → GPIO35 → 2.2kΩ → GND
//  Divider ratio   : 2200 / (1000 + 2200) = 0.6875
//
//  Outputs raw ADC, GPIO voltage, probe voltage, and pH.
//  No GSM, no relay — pure sensor diagnostics.
// ════════════════════════════════════════════════════════════════

// ── Pin ──────────────────────────────────────────────────────────
#define PH_PIN  35   // ADC1 ch7 (input-only)

// ── Voltage divider ──────────────────────────────────────────────
// pH probe AO → R_AO(1kΩ) → GPIO35 → R_GND(2.2kΩ) → GND
static const float R_AO          = 1000.0f;
static const float R_GND         = 2200.0f;
static const float DIVIDER_RATIO = R_GND / (R_AO + R_GND); // 0.6875

// ── Calibration ──────────────────────────────────────────────────
static const float PH_SLOPE  = -8.73f;
static const float PH_OFFSET = 21.83f;
static const float PH_LOW    = 6.0f;
static const float PH_HIGH   = 8.0f;

// ── Sampling ─────────────────────────────────────────────────────
#define PH_SAMPLES  20       // ADC reads per cycle

// ── State ────────────────────────────────────────────────────────
float phValue  = 7.0f;
bool  outOfRange = false;

// ════════════════════════════════════════════════════════════════
//  Setup
// ════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  Serial.println("======================================");
  Serial.println("  pH Probe Debug");
  Serial.println("======================================");
  Serial.printf("  Pin         : GPIO%d (ADC1 ch%d)\n", PH_PIN, PH_PIN - 28);
  Serial.printf("  Divider     : %.0fkΩ (AO→GPIO) + %.0fkΩ (GPIO→GND)\n",
                R_AO / 1000.0f, R_GND / 1000.0f);
  Serial.printf("  Ratio       : %.4f\n", DIVIDER_RATIO);
  Serial.printf("  Slope       : %.2f\n", PH_SLOPE);
  Serial.printf("  Offset      : %.2f\n", PH_OFFSET);
  Serial.printf("  Range       : %.1f – %.1f\n", PH_LOW, PH_HIGH);
  Serial.printf("  Samples     : %d per reading\n", PH_SAMPLES);
  Serial.println("--------------------------------------");
  Serial.println("  Time(ms)   |  ADC_raw | V_GPIO(V) |  V_AO(V)  |   pH   | Status");
  Serial.println("--------------------------------------");
}

// ════════════════════════════════════════════════════════════════
//  Loop — read + print every 1000 ms
// ════════════════════════════════════════════════════════════════

void loop() {
  static unsigned long lastTime = 0;
  unsigned long now = millis();

  if (now - lastTime >= 1000) {
    lastTime = now;

    // ── Burst-sample → average ─────────────────────────────────
    long sum = 0;
    for (int i = 0; i < PH_SAMPLES; i++) {
      sum += analogRead(PH_PIN);
      delay(5);
    }
    float rawADC = (float)sum / PH_SAMPLES;

    // ── Voltage at GPIO35 ──────────────────────────────────────
    float vGPIO = (rawADC / 4095.0f) * 3.3f;

    // ── Reconstruct voltage at probe AO (before divider) ───────
    float vAO = vGPIO / DIVIDER_RATIO;

    // ── pH from calibration ────────────────────────────────────
    phValue    = PH_SLOPE * vGPIO + PH_OFFSET;
    outOfRange = (phValue < PH_LOW || phValue > PH_HIGH);

    // ── Debug output ───────────────────────────────────────────
    Serial.printf("[%8lu] | %8.1f | %8.3f V | %8.3f V | %6.2f | %s\n",
                  now,
                  rawADC,
                  vGPIO,
                  vAO,
                  phValue,
                  outOfRange ? "⚠ OUT OF RANGE" : "✓ IN RANGE");

    // Additional raw info every 10 cycles
    static int cycleCount = 0;
    cycleCount++;
    if (cycleCount % 10 == 0) {
      Serial.println("---");
      Serial.printf("  Raw ADC range over last cycle: ");
      // Quick min/max of last burst (re-sample once for stats)
      int adcMin = 4095, adcMax = 0;
      for (int i = 0; i < 5; i++) {
        int val = analogRead(PH_PIN);
        if (val < adcMin) adcMin = val;
        if (val > adcMax) adcMax = val;
        delay(2);
      }
      Serial.printf("%d – %d (span %d)\n", adcMin, adcMax, adcMax - adcMin);
      Serial.println("---");
    }
  }
}
