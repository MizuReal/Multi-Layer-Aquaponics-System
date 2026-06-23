// ════════════════════════════════════════════════════════════════
//  MQ135 — Air Quality Sensor + Relay (Standalone Debug Sketch)
//
//  Voltage divider: MQ135 AO → 2.2kΩ → GPIO32 → 1kΩ → GND
//  Sensor supply: 5V (VCC_MQ)
//  Relay: GPIO25, active LOW — triggers at POOR (≥ 1000 ppm)
//
//  Math guards prevent overflow:
//    - vAO clamped to sensor VCC (5V)
//    - Rs minimum 1Ω  (prevents pow(0, −b) = ∞)
//    - ratio ∈ [0.001, 1000]
//    - aqPPM ∈ [0, 10000]
//    - Median filter (10 samples) rejects ADC spikes
// ════════════════════════════════════════════════════════════════

// ── Pin ──────────────────────────────────────────────────────────
#define MQ135_PIN      32   // ADC1 ch4

// ── Relay ────────────────────────────────────────────────────────
#define RELAY_AIR      25   // active LOW
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ── Voltage divider ──────────────────────────────────────────────
// MQ135 AO → 2.2kΩ → GPIO32 → 1kΩ → GND
static const float R1   = 2200.0f;   // AO → GPIO32 (Ω)
static const float R2   = 1000.0f;   // GPIO32 → GND  (Ω)
static const float VCC  = 5.0f;      // MQ135 sensor supply (V)
static const float RO   = 5417.0f;   // Calibrated Ro (Ω) — update if drift
static const float PARA = 7905.5f;   // CO₂ curve coefficient A
static const float PARB = 2.862f;    // CO₂ curve coefficient B

// ── Thresholds ───────────────────────────────────────────────────
#define SAMPLES         10           // median filter window
#define AQ_GOOD         400.0f       // ppm
#define AQ_MODERATE     700.0f
#define AQ_POOR        1000.0f       // relay ON above this
#define AQ_BAD         2000.0f

// ── State ────────────────────────────────────────────────────────
float aqPPM     = 400.0f;
float aqPercent = 100.0f;
bool  relayOn   = false;
const char* AQ_LABELS[] = { "GOOD", "MODERATE", "POOR", "BAD", "HAZARDOUS" };
int   labelIdx  = 0;

// ════════════════════════════════════════════════════════════════
//  Median filter (bubble sort on float array)
// ════════════════════════════════════════════════════════════════

float medianOf(float* buf, int size) {
  float tmp[SAMPLES];
  memcpy(tmp, buf, size * sizeof(float));
  for (int i = 0; i < size - 1; i++)
    for (int j = 0; j < size - i - 1; j++)
      if (tmp[j] > tmp[j+1]) { float t = tmp[j]; tmp[j] = tmp[j+1]; tmp[j+1] = t; }
  return tmp[size / 2];
}

// ════════════════════════════════════════════════════════════════
//  Relay — no-op if unchanged
// ════════════════════════════════════════════════════════════════

void setRelay(bool on) {
  if (on == relayOn) return;
  relayOn = on;
  digitalWrite(RELAY_AIR, on ? RELAY_ON : RELAY_OFF);
}

// ════════════════════════════════════════════════════════════════
//  MQ135 compute — median + math guards
// ════════════════════════════════════════════════════════════════

void computeMQ135() {
  // ── Burst-sample → median ────────────────────────────────────
  float rawSamples[SAMPLES];
  for (int i = 0; i < SAMPLES; i++) {
    rawSamples[i] = (float)analogRead(MQ135_PIN);
    delay(5);
  }
  float raw = medianOf(rawSamples, SAMPLES);

  float vADC = (raw / 4095.0f) * 3.3f;
  float vAO  = vADC * (R1 + R2) / R2;

  // ── Clamp vAO — physically cannot exceed sensor VCC ───────────
  if (vAO > VCC) vAO = VCC;

  // ── Guard: open circuit / dead sensor ─────────────────────────
  if (vAO < 0.01f) return;

  float Rs = ((VCC - vAO) / vAO) * R2;

  // ── Minimum Rs prevents ratio → 0 → pow(0,−b) = ∞ ────────────
  if (Rs < 1.0f)   Rs = 1.0f;
  if (Rs > 1e6f)   Rs = 1e6f;

  float ratio = Rs / RO;

  // ── Clamp ratio bounds the power-law domain ───────────────────
  if (ratio < 0.001f)  ratio = 0.001f;
  if (ratio > 1000.0f) ratio = 1000.0f;

  aqPPM = PARA * pow(ratio, -PARB);

  // ── Cap result to sane range ──────────────────────────────────
  if (aqPPM < 0.0f)      aqPPM = 0.0f;
  if (aqPPM > 10000.0f)  aqPPM = 10000.0f;

  aqPercent = constrain(
    (1.0f - (aqPPM - AQ_GOOD) / (AQ_BAD - AQ_GOOD)) * 100.0f,
    0.0f, 100.0f
  );

  if      (aqPPM < AQ_GOOD)      labelIdx = 0;
  else if (aqPPM < AQ_MODERATE)  labelIdx = 1;
  else if (aqPPM < AQ_POOR)      labelIdx = 2;
  else if (aqPPM < AQ_BAD)       labelIdx = 3;
  else                            labelIdx = 4;

  setRelay(aqPPM >= AQ_POOR);
}

// ════════════════════════════════════════════════════════════════
//  Setup
// ════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  digitalWrite(RELAY_AIR, RELAY_OFF);
  pinMode(RELAY_AIR, OUTPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);

  Serial.println("==================================");
  Serial.println("  MQ135 Air Quality + Relay Test");
  Serial.println("==================================");
  Serial.printf("  Pin       : GPIO%d\n", MQ135_PIN);
  Serial.printf("  Relay     : GPIO%d (ON at >= %.0f ppm)\n", RELAY_AIR, AQ_POOR);
  Serial.printf("  Divider   : %.0fkΩ + %.0fkΩ\n", R1 / 1000.0f, R2 / 1000.0f);
  Serial.printf("  Ro        : %.0f Ω\n", RO);
  Serial.println("----------------------------------");
}

// ════════════════════════════════════════════════════════════════
//  Loop
// ════════════════════════════════════════════════════════════════

void loop() {
  static unsigned long lastTime = 0;
  unsigned long now = millis();

  if (now - lastTime >= 2000) {
    lastTime = now;
    computeMQ135();

    Serial.printf("[%8lu ms] ", now);
    Serial.printf("CO₂ equiv: %7.1f ppm | ", aqPPM);
    Serial.printf("Quality: %5.1f%% (%s) | ", aqPercent, AQ_LABELS[labelIdx]);
    Serial.printf("Relay: %s\n", relayOn ? "ON " : "OFF");
  }
}
