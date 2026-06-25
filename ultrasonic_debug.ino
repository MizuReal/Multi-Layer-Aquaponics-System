// ════════════════════════════════════════════════════════════════
//  Ultrasonic AJ-SR04M — Standalone Debug Sketch
//
//  Pins:  TRIG=GPIO5   ECHO=GPIO18 (voltage divider: 2.2kΩ+1kΩ)
//  Blind zone: ~20 cm.  Max range: ~450 cm.
// ════════════════════════════════════════════════════════════════

#define TRIG_PIN       5
#define ECHO_PIN      18

#define SAMPLES        5       // median filter window
#define SENSOR_HEIGHT  35.0    // cm — distance from sensor to tank bottom
#define TARGET_LEVEL   10.0    // desired water level (cm)

// ════════════════════════════════════════════════════════════════

float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 38000);   // 38 ms timeout
  if (duration == 0) return -1.0;
  return (duration * 0.0343) / 2.0;
}

float medianDistance() {
  float samples[SAMPLES];
  for (int i = 0; i < SAMPLES; i++) {
    samples[i] = readDistanceCM();
    delay(30);
  }
  // Bubble sort
  for (int i = 0; i < SAMPLES - 1; i++)
    for (int j = i + 1; j < SAMPLES; j++)
      if (samples[j] < samples[i]) {
        float tmp = samples[i];
        samples[i] = samples[j];
        samples[j] = tmp;
      }
  // Max reading — water surface is always farthest from sensor
  return samples[SAMPLES - 1];
}

// ════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Serial.println(F("======================================"));
  Serial.println(F("  Ultrasonic AJ-SR04M Debug"));
  Serial.println(F("======================================"));
  Serial.println(F("  TRIG = GPIO5    ECHO = GPIO18"));
  Serial.println(F("  Blind zone: ~20 cm"));
  Serial.println(F("  Divider: 2.2kΩ + 1kΩ (5V→3.3V)"));
  Serial.println(F("──────────────────────────────────────"));
  Serial.println(F("  Time(ms)  |  Distance(cm)  |  Water(cm)  |  Status"));
  Serial.println(F("──────────────────────────────────────"));
}

void loop() {
  static unsigned long lastTime = 0;

  if (millis() - lastTime >= 1000) {
    lastTime = millis();

    float dist = medianDistance();
    float water = SENSOR_HEIGHT - dist;

    Serial.printf("[%8lu] | ", millis());

    if (dist < 0) {
      Serial.print(F("  NO ECHO     |     —     |  ⚠ Timeout (out of range / no target)"));
    } else if (dist < 2) {
      Serial.printf("  %5.1f cm    |     —     |  ⚠ Too close (< 2 cm)", dist);
    } else if (dist < 20) {
      Serial.printf("  %5.1f cm    |  %5.1f cm  |  ⚠ Inside blind zone (< 20 cm)", dist, water);
    } else {
      Serial.printf("  %5.1f cm    |  %5.1f cm  |  %s", dist, water,
                      water >= TARGET_LEVEL ? "✓ OK" : "⚠ Water below target");
    }

    Serial.println();

    // Quick test: cover sensor with hand at different distances
    // Watch the readings change in real time
  }
}
