#define TRIG_PIN     5
#define ECHO_PIN     18
#define RELAY_PIN    23

// AJ-SR04M with voltage divider (2.2kΩ + 1kΩ) on ECHO line
// Sensor mounted 20cm from bottom of tank
// Target water level: 10cm from sensor = 10cm of water

const float SENSOR_HEIGHT_CM   = 20.0;  // Distance from sensor to tank bottom
const float TARGET_LEVEL_CM    = 10.0;  // Desired water level (from bottom)
const float RELAY_ON_DURATION  = 500;   // ms debounce before toggling relay
const int   MEDIAN_SAMPLES     = 5;     // Samples for median filter

float readDistanceCM() {
  // Trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // AJ-SR04M echo timeout ~38ms (max range ~450cm)
  long duration = pulseIn(ECHO_PIN, HIGH, 38000);

  if (duration == 0) return -1.0;  // No echo / out of range

  // Voltage divider scales 5V echo to 3.3V-safe:
  // No correction needed — duration is time-based, not voltage-based
  return (duration * 0.0343) / 2.0;
}

float medianDistance() {
  float samples[MEDIAN_SAMPLES];

  for (int i = 0; i < MEDIAN_SAMPLES; i++) {
    samples[i] = readDistanceCM();
    delay(30);
  }

  // Bubble sort
  for (int i = 0; i < MEDIAN_SAMPLES - 1; i++) {
    for (int j = i + 1; j < MEDIAN_SAMPLES; j++) {
      if (samples[j] < samples[i]) {
        float tmp = samples[i];
        samples[i] = samples[j];
        samples[j] = tmp;
      }
    }
  }

  return samples[MEDIAN_SAMPLES / 2];
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN,  OUTPUT);
  pinMode(ECHO_PIN,  INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(TRIG_PIN,  LOW);
  digitalWrite(RELAY_PIN, HIGH);  // Relay OFF (active-low assumed; flip if active-high)
}

void loop() {
  float distanceCM = medianDistance();

  if (distanceCM < 0) {
    Serial.println("[WARN] No echo — sensor out of range or disconnected");
    delay(1000);
    return;
  }

  // Water level = tank height minus distance from sensor to water surface
  float waterLevelCM = SENSOR_HEIGHT_CM - distanceCM;

  Serial.printf("[SONAR] Distance: %.2f cm | Water Level: %.2f cm | Target: %.2f cm\n",
                distanceCM, waterLevelCM, TARGET_LEVEL_CM);

  if (waterLevelCM < TARGET_LEVEL_CM) {
    // Water too low — pump ON
    digitalWrite(RELAY_PIN, LOW);   // Active-low relay: LOW = ON
    Serial.println("[RELAY] Pump ON — filling");
  } else {
    // Water at or above target — pump OFF
    digitalWrite(RELAY_PIN, HIGH);  // Active-low relay: HIGH = OFF
    Serial.println("[RELAY] Pump OFF — level OK");
  }

  delay(RELAY_ON_DURATION);
}