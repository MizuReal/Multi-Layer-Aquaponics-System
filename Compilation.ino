// ════════════════════════════════════════════════════════════════
//  Multi-Layer Aquaponic for Crayfish and Fish (Zebra Danios) System
//  ESP32 Merged Firmware — Water Quality + Air Quality (MQ135)
//                         + Ultrasonic Water Level + Temp Relay
//
//  Developers:
//    Acosta, Mark Girone, C.
//    Antoniano, Ryan Russel A.
//    Bumatay, Axel Jillian C.
//    Cruz, Hanna Clerdee E.
//    Danque, John Michael G.
//    Yebes, Kim Jensen, B.
//
//  ── Water Sensors ───────────────────────────────────────────────
//  TDS probe    : GPIO34 (ADC1 ch6)
//  pH probe     : GPIO35 (ADC1 ch7, input-only)
//                 voltage divider: 1kΩ AO→GPIO35→2.2kΩ GND
//  BH1750       : I²C — SDA=GPIO21, SCL=GPIO22
//  DS18B20      : GPIO4, 4.7kΩ pull-up to 3.3V
//
//  ── Air Quality Sensor ──────────────────────────────────────────
//  MQ135        : GPIO32 (ADC1 ch4)
//                 voltage divider: AO → 2.2kΩ → GPIO32 → 1kΩ → GND
//                 sensor supply: 5V
//
//  ── Ultrasonic Water Level ──────────────────────────────────────
//  AJ-SR04M     : TRIG=GPIO5, ECHO=GPIO18
//                 ECHO voltage divider: 2.2kΩ + 1kΩ (5V→3.3V)
//  Water pump   : GPIO23 (active LOW)
//
//  ── Actuators ───────────────────────────────────────────────────
//  Relay1       : Flush pump      (GPIO26, active LOW)
//  Relay2       : UV lamp         (GPIO27, active LOW)
//  Relay3       : Air quality     (GPIO25, active LOW)
//  Relay4       : Water fill pump (GPIO23, active LOW)
//  Relay5       : Temperature ctrl(GPIO19, active LOW)
//
//  ── GSM ─────────────────────────────────────────────────────────
//  SIM800L on UART2
//    ESP32 GPIO16 TX  ──→ SIM800L RXD
//    ESP32 GPIO17 RX ←── SIM800L TXD
//
//  ── Dashboard ───────────────────────────────────────────────────
//  HTTP WebServer on port 80
//    GET /      → dashboard (HTML)
//    GET /data  → JSON sensor snapshot
// ════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <Wire.h>
#include <BH1750.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HardwareSerial.h>
#include <math.h>
#define MQTT_MAX_PACKET_SIZE 1024  // Must precede PubSubClient.h
#include <PubSubClient.h>

// ── WiFi credentials ─────────────────────────────────────────────
#define WIFI_SSID  "danque"
#define WIFI_PASS  "sap2ns3a"

// ── Pin definitions ──────────────────────────────────────────────
#define TDS_PIN        34   // TDS probe        (ADC1 ch6)
#define PH_PIN         35   // pH probe         (ADC1 ch7, input-only)
#define MQ135_PIN      32   // MQ135 air quality (ADC1 ch4)
#define RELAY_PUMP     26   // Flush pump       (active LOW)
#define RELAY_UV       27   // UV lamp          (active LOW)
#define RELAY_AIR      25   // Air quality relay(active LOW)
#define RELAY_TEMP     19   // Temperature ctrl  (active LOW)
#define RELAY_WATER    23   // Water fill pump   (active LOW)
#define ONE_WIRE_BUS    4   // DS18B20 data
#define TRIG_PIN        5   // Ultrasonic TRIG
#define ECHO_PIN       18   // Ultrasonic ECHO   (voltage divider: 2.2kΩ+1kΩ)
// BH1750 : SDA=GPIO21, SCL=GPIO22
// GSM    : TX→RXD=GPIO16, RX←TXD=GPIO17

// ── Relay polarity ───────────────────────────────────────────────
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ── TDS config ───────────────────────────────────────────────────
#define VREF            3.3f
#define ADC_RES         4096.0f
#define TDS_SAMPLES     30
#define TDS_THRESH      300.0f    // ppm — above triggers flush pump

// ── pH config ────────────────────────────────────────────────────
#define PH_SAMPLES      20
static const float R_AO          = 1000.0f;
static const float R_GND         = 2200.0f;
static const float DIVIDER_RATIO = R_GND / (R_AO + R_GND); // 0.6875
static const float PH_SLOPE      = -8.58f;
static const float PH_OFFSET     = 20.22f;
static const float PH_LOW        = 6.0f;
static const float PH_HIGH       = 8.0f;

// ── Light config ─────────────────────────────────────────────────
#define LUX_THRESH      10.0f     // lux — below triggers UV lamp

// ── MQ135 config ─────────────────────────────────────────────────
// Voltage divider: AO → R1(2.2kΩ) → GPIO32 → R2(1kΩ) → GND
// Sensor supply: 5V (VCC_MQ)
static const float MQ135_R1      = 2200.0f;   // AO → GPIO32 (Ω)
static const float MQ135_R2      = 1000.0f;   // GPIO32 → GND (Ω)
static const float MQ135_VCC     = 5.0f;      // MQ135 sensor supply (V)
static const float MQ135_RO      = 5417.0f;   // Calibrated Ro (Ω) — update if sensor drifts
static const float MQ135_PARA    = 7905.5f;   // CO₂ curve coefficient A
static const float MQ135_PARB    = 2.862f;    // CO₂ curve coefficient B
#define MQ135_SAMPLES   10
#define AQ_GOOD         400.0f    // ppm
#define AQ_MODERATE     700.0f
#define AQ_POOR        1000.0f
#define AQ_BAD         2000.0f

// ── GSM config ───────────────────────────────────────────────────
#define GSM_SERIAL      Serial2
#define GSM_TX          17          // ESP32 TX → SIM800L RXD
#define GSM_RX          16          // ESP32 RX ← SIM800L TXD
#define ALERT_INTERVAL  (10UL * 60UL * 1000UL)   // 10 min between alerts
const char RECIPIENT[]  = "+639945949061";

// ── MQTT config ───────────────────────────────────────────────────
#define MQTT_BROKER      "172.20.10.4"  // Raspberry Pi IP — update
#define MQTT_PORT         1883
#define MQTT_CLIENT_ID    "ESP32_Aquaponic"
#define MQTT_TOPIC_DATA   "aquaponic/data"
#define MQTT_TOPIC_STATUS "aquaponic/status"
#define MQTT_PUB_INTERVAL 2000          // ms (matches dashboard refresh)

// ── Ultrasonic config ────────────────────────────────────────────
// AJ-SR04M sensor with voltage divider on ECHO line
// Sensor mounted at top of tank, facing down
static const float SENSOR_HEIGHT_CM = 35.0f;   // sensor distance to tank bottom (cm)
static const float TARGET_LEVEL_CM  = 10.0f;   // desired water level from bottom (cm)
#define SONAR_SAMPLES   5                      // median filter sample count

// ── Temperature relay config ─────────────────────────────────────
#define TEMP_THRESHOLD  28.0f   // °C — relay ON above this threshold

// ── Sensor objects ───────────────────────────────────────────────
BH1750             lightMeter;
OneWire            oneWire(ONE_WIRE_BUS);
DallasTemperature  tempSensor(&oneWire);
WiFiClient         mqttWiFiClient;
PubSubClient       mqttClient(mqttWiFiClient);

// ── Water sensor state ───────────────────────────────────────────
int   tdsBuffer[TDS_SAMPLES];
int   tdsBufferIdx  = 0;
int   tdsFillCount  = 0;

float waterTemp     = 25.0f;
float tdsValue      = 0.0f;
float phValue       = 7.0f;
float lastLux       = 0.0f;
bool  pumpOn        = false;   // flush pump
bool  uvOn          = false;
bool  bh1750Ok      = false;   // set by begin() in setup

// ── MQ135 air quality state ──────────────────────────────────────
float aqPPM         = 400.0f;
float aqPercent     = 100.0f;
bool  airRelayOn    = false;
// 0=GOOD 1=MODERATE 2=POOR 3=BAD 4=HAZARDOUS
int   aqLabelIdx    = 0;
static const char* AQ_LABELS[] = { "GOOD", "MODERATE", "POOR", "BAD", "HAZARDOUS" };

// ── GSM state ────────────────────────────────────────────────────
bool  gsmReady      = false;
int   gsmRSSI       = 0;
int   gsmVoltage    = 0;          // mV from AT+CBC
unsigned long lastAlertTime   = 0;
bool  wasLow        = false;   // pH was below PH_LOW on previous tick
bool  wasHigh       = false;   // pH was above PH_HIGH on previous tick
uint32_t smsSent              = 0;

// ── Ultrasonic state ─────────────────────────────────────────────
float sonarDistance  = 0.0f;     // distance from sensor to water surface (cm)
float waterLevelCm   = 0.0f;     // computed water level from bottom (cm)
bool  waterPumpOn    = false;    // water fill pump relay state

// ── Temperature relay state ──────────────────────────────────────
bool  tempRelayOn    = false;    // temperature control relay state

// ════════════════════════════════════════════════════════════════
//  Helpers — TDS
// ════════════════════════════════════════════════════════════════

int medianOf(int* buf, int size) {
  int tmp[TDS_SAMPLES];
  memcpy(tmp, buf, size * sizeof(int));
  for (int i = 0; i < size - 1; i++)
    for (int j = 0; j < size - i - 1; j++)
      if (tmp[j] > tmp[j+1]) { int t = tmp[j]; tmp[j] = tmp[j+1]; tmp[j+1] = t; }
  return tmp[size / 2];
}

void computeTDS() {
  int   med    = medianOf(tdsBuffer, TDS_SAMPLES);
  float v      = (med / ADC_RES) * VREF;
  float coeff  = 1.0f + 0.02f * (waterTemp - 25.0f);
  float vc     = v / coeff;
  tdsValue = (133.42f * pow(vc, 3) - 255.86f * pow(vc, 2) + 857.39f * vc) * 0.5f;
  if (tdsValue < 0.0f) tdsValue = 0.0f;
}

// ════════════════════════════════════════════════════════════════
//  Helpers — Relay (no-op if unchanged)
// ════════════════════════════════════════════════════════════════

void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  digitalWrite(RELAY_PUMP, on ? RELAY_ON : RELAY_OFF);
}

void setUV(bool on) {
  if (on == uvOn) return;
  uvOn = on;
  digitalWrite(RELAY_UV, on ? RELAY_ON : RELAY_OFF);
}

void setAirRelay(bool on) {
  if (on == airRelayOn) return;
  airRelayOn = on;
  digitalWrite(RELAY_AIR, on ? RELAY_ON : RELAY_OFF);
}

void setWaterPump(bool on) {
  if (on == waterPumpOn) return;
  waterPumpOn = on;
  digitalWrite(RELAY_WATER, on ? RELAY_ON : RELAY_OFF);
}

void setTempRelay(bool on) {
  if (on == tempRelayOn) return;
  tempRelayOn = on;
  digitalWrite(RELAY_TEMP, on ? RELAY_ON : RELAY_OFF);
}

// ════════════════════════════════════════════════════════════════
//  MQ135 — compute air quality from ADC reading
//  Phase 1: math guards prevent overflow (vAO clamp, Rs/ratio caps)
//  Phase 2: median filter rejects ADC spikes (replaces averaging)
// ════════════════════════════════════════════════════════════════

// Bubble-sort for small burst; callable before medianOf (which works on int*)
static float medianOfFloat(float* buf, int size) {
  float tmp[MQ135_SAMPLES];
  memcpy(tmp, buf, size * sizeof(float));
  for (int i = 0; i < size - 1; i++)
    for (int j = 0; j < size - i - 1; j++)
      if (tmp[j] > tmp[j+1]) { float t = tmp[j]; tmp[j] = tmp[j+1]; tmp[j+1] = t; }
  return tmp[size / 2];
}

void computeMQ135() {
  // ── Phase 2: burst-sample → median (rejects spikes) ─────────
  float rawSamples[MQ135_SAMPLES];
  for (int i = 0; i < MQ135_SAMPLES; i++) {
    rawSamples[i] = (float)analogRead(MQ135_PIN);
    delay(5);
  }
  float raw = medianOfFloat(rawSamples, MQ135_SAMPLES);

  float vADC = (raw / 4095.0f) * 3.3f;
  float vAO  = vADC * (MQ135_R1 + MQ135_R2) / MQ135_R2;

  // ── Phase 1: clamp vAO — it physically cannot exceed sensor VCC ─
  if (vAO > MQ135_VCC) vAO = MQ135_VCC;

  // Guard: open circuit / dead sensor
  if (vAO < 0.01f) return;

  float Rs    = ((MQ135_VCC - vAO) / vAO) * MQ135_R2;

  // ── Phase 1: minimum Rs prevents ratio→0 → pow(0,−b)=∞ ─────
  if (Rs < 1.0f)   Rs = 1.0f;
  if (Rs > 1e6f)   Rs = 1e6f;

  float ratio = Rs / MQ135_RO;

  // ── Phase 1: clamp ratio bounds the power-law domain ─────────
  if (ratio < 0.001f)  ratio = 0.001f;
  if (ratio > 1000.0f) ratio = 1000.0f;

  aqPPM = MQ135_PARA * pow(ratio, -MQ135_PARB);

  // ── Phase 1: cap result to sane range ────────────────────────
  if (aqPPM < 0.0f)      aqPPM = 0.0f;
  if (aqPPM > 10000.0f)  aqPPM = 10000.0f;

  aqPercent = constrain(
    (1.0f - (aqPPM - AQ_GOOD) / (AQ_BAD - AQ_GOOD)) * 100.0f,
    0.0f, 100.0f
  );

  if      (aqPPM < AQ_GOOD)      aqLabelIdx = 0;
  else if (aqPPM < AQ_MODERATE)  aqLabelIdx = 1;
  else if (aqPPM < AQ_POOR)      aqLabelIdx = 2;
  else if (aqPPM < AQ_BAD)       aqLabelIdx = 3;
  else                            aqLabelIdx = 4;

  setAirRelay(aqPPM >= AQ_POOR);
}

// ════════════════════════════════════════════════════════════════
//  Ultrasonic — AJ-SR04M water level
// ════════════════════════════════════════════════════════════════

float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 38000);
  if (duration == 0) return -1.0f;
  return (duration * 0.0343f) / 2.0f;
}

float medianDistance() {
  float samples[SONAR_SAMPLES];
  for (int i = 0; i < SONAR_SAMPLES; i++) {
    samples[i] = readDistanceCM();
    delay(30);
  }
  for (int i = 0; i < SONAR_SAMPLES - 1; i++)
    for (int j = i + 1; j < SONAR_SAMPLES; j++)
      if (samples[j] < samples[i]) {
        float tmp = samples[i];
        samples[i] = samples[j];
        samples[j] = tmp;
      }
  // Max reading — water surface is always farthest from sensor
  return samples[SONAR_SAMPLES - 1];
}

// ════════════════════════════════════════════════════════════════
//  GSM helpers — SIM800L via UART2
// ════════════════════════════════════════════════════════════════

void flushGSM() {
  while (GSM_SERIAL.available()) GSM_SERIAL.read();
}

String sendAT(const char* cmd, unsigned long tms = 3000) {
  flushGSM();
  GSM_SERIAL.println(cmd);
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < tms) {
    while (GSM_SERIAL.available()) resp += (char)GSM_SERIAL.read();
    if (resp.indexOf("OK")    != -1 ||
        resp.indexOf("ERROR") != -1 ||
        resp.indexOf(">")     != -1) break;
  }
  resp.trim();
  return resp;
}

bool sendATExpect(const char* cmd, const char* expected, uint32_t timeout_ms) {
  flushGSM();
  GSM_SERIAL.println(cmd);
  uint32_t start = millis();
  String resp;
  while (millis() - start < timeout_ms) {
    while (GSM_SERIAL.available()) resp += (char)GSM_SERIAL.read();
    if (resp.indexOf(expected) != -1) return true;
    if (resp.indexOf("ERROR") != -1) return false;
    delay(10);
  }
  return false;
}

bool autoBaudGSM() {
  const long bauds[] = {9600, 19200, 38400, 57600, 115200};
  for (int b = 0; b < 5; b++) {
    GSM_SERIAL.begin(bauds[b], SERIAL_8N1, GSM_RX, GSM_TX);
    delay(150);
    flushGSM();
    // Autobaud sync: 3 blind AT pings
    GSM_SERIAL.println("AT"); delay(300);
    GSM_SERIAL.println("AT"); delay(300);
    GSM_SERIAL.println("AT"); delay(300);
    flushGSM();
    // Real probe
    GSM_SERIAL.println("AT");
    delay(600);
    String r;
    while (GSM_SERIAL.available()) r += (char)GSM_SERIAL.read();
    r.trim();
    if (r.indexOf("OK") != -1 || r.indexOf("AT") != -1) return true;
  }
  GSM_SERIAL.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  return false;
}

bool checkGSMBattery() {
  String r = sendAT("AT+CBC", 3000);
  r.trim();
  int idx = r.indexOf("+CBC:");
  if (idx == -1) return false;
  String fields = r.substring(idx + 5);
  fields.trim();
  int c1 = fields.indexOf(',');
  int c2 = fields.indexOf(',', c1 + 1);
  if (c1 == -1 || c2 == -1) return false;
  gsmVoltage = fields.substring(c2 + 1).toInt();
  return (gsmVoltage >= 3500);
}

bool checkGSMSIM() {
  String r = sendAT("AT+CPIN?", 5000);
  return (r.indexOf("READY") != -1);
}

bool checkGSMNetwork() {
  String r = sendAT("AT+CREG?", 5000);
  Serial.println("CREG raw: '" + r + "'");
  return (r.indexOf(",1") != -1 || r.indexOf(",5") != -1);
}

bool initGSM() {
  delay(2000);   // 2s head start — autoBaud scan overlaps with boot
  if (!autoBaudGSM()) { Serial.println("GSM: autoBaud FAIL"); return false; }
  Serial.print("GSM: autoBaud OK, free heap=");
  Serial.println(ESP.getFreeHeap());

  sendATExpect("ATE0", "OK", 2000);
  sendATExpect("AT+CMEE=2", "OK", 2000);
  checkGSMSIM();

  Serial.print("GSM: network reg");
  bool reg = false;
  for (int i = 0; i < 15 && !reg; i++) {
    reg = checkGSMNetwork();
    Serial.print(reg ? "" : ".");
    if (!reg) delay(2000);
  }
  Serial.println(reg ? " OK" : " FAIL");

  if (reg) {
    String csq = sendAT("AT+CSQ");
    Serial.println("GSM: CSQ raw=" + csq);
    int idx = csq.indexOf("+CSQ:");
    if (idx != -1) gsmRSSI = csq.substring(idx + 5).toInt();
    Serial.printf("GSM: RSSI=%d\n", gsmRSSI);
  }

  return reg;
}

void sendSMS(float ph) {
  const char* dir = (ph < PH_LOW) ? "LOW - too acidic" : "HIGH - too alkaline";
  char msg[140];
  snprintf(msg, sizeof(msg),
    "pH ALERT\nReading : %.2f (%s)\nRange   : %.1f - %.1f\nCheck the system.",
    ph, dir, PH_LOW, PH_HIGH);

  sendATExpect("AT+CMGF=1", "OK", 3000);
  sendATExpect("AT+CSCS=\"GSM\"", "OK", 2000);

  flushGSM();
  char cmd[60];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", RECIPIENT);
  GSM_SERIAL.println(cmd);

  unsigned long start = millis();
  bool gotPrompt = false;
  String promptBuf;
  while (millis() - start < 8000) {
    while (GSM_SERIAL.available()) {
      char c = GSM_SERIAL.read();
      promptBuf += c;
      if (c == '>') { gotPrompt = true; break; }
    }
    if (gotPrompt) break;
    if (promptBuf.indexOf("ERROR") != -1) return;
    delay(10);
  }
  if (!gotPrompt) return;

  delay(100);
  GSM_SERIAL.print(msg);
  GSM_SERIAL.write(0x1A);

  start = millis();
  String resp;
  while (millis() - start < 15000) {
    while (GSM_SERIAL.available()) resp += (char)GSM_SERIAL.read();
    if (resp.indexOf("+CMGS:") != -1) { smsSent++; return; }
    if (resp.indexOf("ERROR") != -1) return;
    delay(10);
  }
}

// ════════════════════════════════════════════════════════════════
//  MQTT — JSON builder + broker connection
// ════════════════════════════════════════════════════════════════

void buildSensorJSON(char* buf, size_t bufSize) {
  const char* tdsQ =
    tdsValue < 50  ? "Excellent" :
    tdsValue < 150 ? "Good"      :
    tdsValue < 300 ? "Fair"      :
    tdsValue < 600 ? "Poor"      : "Bad";

  snprintf(buf, bufSize,
    "{"
      "\"tds\":%.1f,"
      "\"ph\":%.2f,"
      "\"temp\":%.2f,"
      "\"lux\":%.1f,"
      "\"pump\":%s,"
      "\"uv\":%s,"
      "\"tds_quality\":\"%s\","
      "\"gsm_ready\":%s,"
      "\"rssi\":%d,"
      "\"sms_sent\":%lu,"
      "\"aq_ppm\":%.1f,"
      "\"aq_pct\":%.1f,"
      "\"aq_label\":\"%s\","
      "\"air_relay\":%s,"
      "\"temp_relay\":%s,"
      "\"sonar_dist\":%.2f,"
      "\"water_level\":%.2f,"
      "\"water_pump\":%s,"
      "\"gsm_volt\":%d"
    "}",
    tdsValue, phValue, waterTemp, lastLux,
    pumpOn     ? "true" : "false",
    uvOn       ? "true" : "false",
    tdsQ,
    gsmReady   ? "true" : "false",
    gsmRSSI,
    (unsigned long)smsSent,
    aqPPM, aqPercent,
    AQ_LABELS[aqLabelIdx],
    airRelayOn ? "true" : "false",
    tempRelayOn ? "true" : "false",
    sonarDistance,
    waterLevelCm,
    waterPumpOn ? "true" : "false",
    gsmVoltage
  );
}

bool connectMQTT() {
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_TOPIC_STATUS, 0, true, "offline")) {
    mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
    return true;
  }
  return false;
}

// ════════════════════════════════════════════════════════════════
//  Setup
// ════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  // Relays — set output state BEFORE pinMode to avoid glitch
  digitalWrite(RELAY_PUMP,  RELAY_OFF);
  digitalWrite(RELAY_UV,    RELAY_OFF);
  digitalWrite(RELAY_AIR,   RELAY_OFF);
  digitalWrite(RELAY_TEMP,  RELAY_OFF);
  digitalWrite(RELAY_WATER, RELAY_OFF);
  pinMode(RELAY_PUMP,  OUTPUT);
  pinMode(RELAY_UV,    OUTPUT);
  pinMode(RELAY_AIR,   OUTPUT);
  pinMode(RELAY_TEMP,  OUTPUT);
  pinMode(RELAY_WATER, OUTPUT);

  // ADC — configure all three ADC pins before first read
  analogReadResolution(12);
  analogSetPinAttenuation(TDS_PIN,   ADC_11db);
  analogSetPinAttenuation(PH_PIN,    ADC_11db);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);

  // Pre-fill TDS ring buffer
  for (int i = 0; i < TDS_SAMPLES; i++) tdsBuffer[i] = 1200;

  // DS18B20 — async mode (non-blocking)
  tempSensor.begin();
  tempSensor.setResolution(12);
  tempSensor.setWaitForConversion(false);
  tempSensor.requestTemperatures();

  // Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // WiFi event callbacks
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    WiFi.reconnect();
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // GSM init (match GSM800.ino timing)
  GSM_SERIAL.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  gsmReady = initGSM();

  // Wait up to 10s for WiFi
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long wt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wt < 10000) delay(500);
  }

  // I²C + BH1750 (after GSM/WiFi settled — avoids I²C bus corruption)
  Wire.begin(21, 22);
  Wire.setClock(50000);
  bh1750Ok = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23);
  delay(200);   // BH1750 power-on spec (180 ms)

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  if (connectMQTT())
    Serial.println("MQTT: connected to broker");
  else
    Serial.println("MQTT: FAILED to connect to broker — check IP/port");
}

// ════════════════════════════════════════════════════════════════
//  Loop — independent non-blocking timers
//
//   Timer   Period   Task
//   ─────   ──────   ────────────────────────────────────────────
//     1      40 ms   TDS ADC sample into ring buffer
//     2     800 ms   Compute TDS, update flush pump relay
//     3    1200 ms   BH1750 lux read, update UV relay
//     4    2000 ms   DS18B20 async read + re-request, temp relay
//     5    1000 ms   pH ADC read, GSM alert check
//     6    5000 ms   MQ135 air quality read, air relay
//     7    2000 ms   Ultrasonic water level, water fill pump relay
//     8    2000 ms   MQTT publish sensor data to broker
// ════════════════════════════════════════════════════════════════

void loop() {
  mqttClient.loop();   // keepalive (non-blocking)
  static unsigned long tSample = 0, tTDS = 0, tLux = 0,
                       tTemp   = 0, tPH  = 0, tAQ  = 0, tSonar = 0, tMQTT = 0;
  unsigned long now = millis();

  // ── Timer 1 · TDS sample (40 ms) ─────────────────────────────
  if (now - tSample >= 40) {
    tSample = now;
    tdsBuffer[tdsBufferIdx] = analogRead(TDS_PIN);
    tdsBufferIdx = (tdsBufferIdx + 1) % TDS_SAMPLES;
    if (tdsFillCount < TDS_SAMPLES) tdsFillCount++;
  }

  // ── Timer 2 · TDS compute + flush pump control (800 ms) ──────
  if (now - tTDS >= 800) {
    tTDS = now;
    computeTDS();
    setPump(tdsValue >= TDS_THRESH);
  }

  // ── Timer 3 · BH1750 lux + UV control (1200 ms) ──────────────
  if (now - tLux >= 1200) {
    tLux  = now;
    lastLux = bh1750Ok ? lightMeter.readLightLevel() : 0.0f;
    setUV(lastLux <= LUX_THRESH);
  }

  // ── Timer 4 · DS18B20 async read + temp relay (2000 ms) ──────
  if (now - tTemp >= 2000) {
    tTemp = now;
    float t = tempSensor.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) waterTemp = t;
    tempSensor.requestTemperatures();

    if (waterTemp > TEMP_THRESHOLD)  setTempRelay(true);
    else                             setTempRelay(false);
  }

  // ── Timer 5 · pH read + GSM alert (1000 ms) ──────────────────
  if (now - tPH >= 1000) {
    tPH = now;

    long sum = 0;
    for (int i = 0; i < PH_SAMPLES; i++) {
      sum += analogRead(PH_PIN);
      delay(5);
    }
    float raw  = (float)sum / PH_SAMPLES;
    float vadc = (raw / 4095.0f) * 3.3f;
    phValue    = PH_SLOPE * vadc + PH_OFFSET;

    bool isLow  = (phValue < PH_LOW);
    bool isHigh = (phValue > PH_HIGH);

    bool newEpisode = (isLow && !wasLow) || (isHigh && !wasHigh);

    if ((isLow || isHigh) && gsmReady && newEpisode) {
      sendSMS(phValue);
      lastAlertTime = now;
      String csq = sendAT("AT+CSQ");
      int ci = csq.indexOf("+CSQ:");
      if (ci != -1) gsmRSSI = csq.substring(ci + 5).toInt();
    } else if ((isLow || isHigh) && gsmReady && (now - lastAlertTime >= ALERT_INTERVAL)) {
      sendSMS(phValue);
      lastAlertTime = now;
      String csq = sendAT("AT+CSQ");
      int ci = csq.indexOf("+CSQ:");
      if (ci != -1) gsmRSSI = csq.substring(ci + 5).toInt();
    }

    if (!isLow && !isHigh) lastAlertTime = 0;

    wasLow  = isLow;
    wasHigh = isHigh;
  }

  // ── Timer 6 · MQ135 air quality (5000 ms) ────────────────────
  if (now - tAQ >= 5000) {
    tAQ = now;
    computeMQ135();
  }

  // ── Timer 7 · Ultrasonic water level (2000 ms) ───────────────
  if (now - tSonar >= 2000) {
    tSonar = now;
    sonarDistance = medianDistance();
    if (sonarDistance >= 0) {
      waterLevelCm = SENSOR_HEIGHT_CM - sonarDistance;
      if (waterLevelCm < 0) waterLevelCm = 0;
      setWaterPump(waterLevelCm < TARGET_LEVEL_CM);
    }
  }

  // ── Timer 8 · MQTT publish (2000 ms) ─────────────────────────
  if (now - tMQTT >= MQTT_PUB_INTERVAL) {
    tMQTT = now;
    static bool lastConn = false;
    if (!mqttClient.connected()) {
      connectMQTT();
      if (lastConn) { Serial.println("MQTT: connection lost — reconnecting"); lastConn = false; }
    }
    if (mqttClient.connected()) {
      if (!lastConn) { Serial.println("MQTT: reconnected OK"); lastConn = true; }
      char buf[768];
      buildSensorJSON(buf, sizeof(buf));
      size_t len = strlen(buf);

      if (mqttClient.beginPublish(MQTT_TOPIC_DATA, len, true)) {
        // Chunked write — ESP32 WiFiClient can fail on >128 byte single write
        const size_t CHUNK = 128;
        size_t written = 0;
        bool ok = true;
        while (written < len && ok) {
          size_t toWrite = (len - written) < CHUNK ? (len - written) : CHUNK;
          size_t n = mqttClient.write((const uint8_t*)(buf + written), toWrite);
          if (n == 0) ok = false;
          else written += n;
        }
        mqttClient.endPublish();
        if (ok) {
          static bool firstPub = true;
          if (firstPub) { Serial.printf("MQTT: published OK (%d bytes)\n", len); firstPub = false; }
        } else {
          Serial.printf("MQTT: publish FAILED after %d/%d bytes\n", written, len);
        }
      } else {
        Serial.printf("MQTT: beginPublish FAILED (len=%d)\n", len);
      }
    }
  }
}
