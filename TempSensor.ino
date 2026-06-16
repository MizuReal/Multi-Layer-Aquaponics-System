    #include <OneWire.h>
#include <DallasTemperature.h>

// ── DS18B20 ───────────────────────────────────────────────
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
float waterTemperature = 25.0;

// ── RELAY ─────────────────────────────────────────────────
#define RELAY_PIN 19
#define RELAY_ON  LOW
#define RELAY_OFF HIGH
bool relayState = false;

// ── THRESHOLDS ────────────────────────────────────────────
#define TEMP_HIGH 30.0
#define TEMP_LOW  28.0

// ── RELAY CONTROL ─────────────────────────────────────────
void setRelay(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}

// ── SETUP ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);
  tempSensor.begin();
  tempSensor.setResolution(12);
  tempSensor.setWaitForConversion(false);
  tempSensor.requestTemperatures();
  Serial.println("=================================");
  Serial.println("  DS18B20 Temp + Relay Monitor  ");
  Serial.println("=================================");
  Serial.println("Relay PIN : 19");
  Serial.printf("Thresholds: ON >= %.1f°C | OFF <= %.1f°C\n", TEMP_HIGH, TEMP_LOW);
  Serial.println("---------------------------------");
}

// ── LOOP ──────────────────────────────────────────────────
void loop() {
  static unsigned long lastTempTime = 0;
  const unsigned long TEMP_INTERVAL = 2000;
  unsigned long now = millis();

  if (now - lastTempTime >= TEMP_INTERVAL) {
    lastTempTime = now;
    float t = tempSensor.getTempCByIndex(0);

    if (t != DEVICE_DISCONNECTED_C) {
      waterTemperature = t;
      Serial.printf("[%8lu ms] Temp: %.2f°C | Relay: %s\n",
                    now, waterTemperature, relayState ? "ON " : "OFF");

      if (waterTemperature >= TEMP_HIGH && !relayState) {
        relayState = true;
        setRelay(true);
        Serial.println("         >> RELAY TRIGGERED ON  (temp too HIGH)");
      }
      if (waterTemperature <= TEMP_LOW && relayState) {
        relayState = false;
        setRelay(false);
        Serial.println("         >> RELAY TRIGGERED OFF (temp OK)");
      }
    } else {
      Serial.printf("[%8lu ms] DS18B20 ERROR — check wiring/pull-up\n", now);
    }

    tempSensor.requestTemperatures();
  }
}