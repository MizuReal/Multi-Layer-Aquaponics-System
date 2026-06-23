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
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <BH1750.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HardwareSerial.h>
#include <math.h>

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
static const float PH_SLOPE      = -8.73f;
static const float PH_OFFSET     = 21.83f;
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
#define GSM_TX          16          // ESP32 TX → SIM800L RXD
#define GSM_RX          17          // ESP32 RX ← SIM800L TXD
#define ALERT_INTERVAL  (10UL * 60UL * 1000UL)   // 10 min between alerts
const char RECIPIENT[]  = "+639945949061";

// ── Ultrasonic config ────────────────────────────────────────────
// AJ-SR04M sensor with voltage divider on ECHO line
// Sensor mounted at top of tank, facing down
static const float SENSOR_HEIGHT_CM = 20.0f;   // sensor distance to tank bottom (cm)
static const float TARGET_LEVEL_CM  = 10.0f;   // desired water level from bottom (cm)
#define SONAR_SAMPLES   5                      // median filter sample count

// ── Temperature relay config ─────────────────────────────────────
#define TEMP_HIGH       30.0f   // °C — relay ON above this threshold
#define TEMP_LOW        28.0f   // °C — relay OFF below this threshold

// ── Sensor objects ───────────────────────────────────────────────
BH1750             lightMeter;
OneWire            oneWire(ONE_WIRE_BUS);
DallasTemperature  tempSensor(&oneWire);
AsyncWebServer     server(80);

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
unsigned long outOfRangeStart = 0;
bool  wasOutOfRange           = false;
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
// ════════════════════════════════════════════════════════════════

void computeMQ135() {
  int raw = 0;
  for (int i = 0; i < MQ135_SAMPLES; i++) {
    raw += analogRead(MQ135_PIN);
    delay(5);
  }
  raw /= MQ135_SAMPLES;

  float vADC = (raw / 4095.0f) * 3.3f;
  float vAO  = vADC * (MQ135_R1 + MQ135_R2) / MQ135_R2;

  if (vADC < 0.01f) return;

  float Rs    = ((MQ135_VCC - vAO) / vAO) * MQ135_R2;
  if (Rs < 0.0f) Rs = 0.0f;

  float ratio = Rs / MQ135_RO;
  aqPPM       = MQ135_PARA * pow(ratio, -MQ135_PARB);
  if (aqPPM < 0.0f) aqPPM = 0.0f;

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
  return samples[SONAR_SAMPLES / 2];
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
  return (r.indexOf(",1") != -1 || r.indexOf(",5") != -1);
}

bool initGSM() {
  delay(3000);
  if (!autoBaudGSM()) return false;

  sendATExpect("ATE0", "OK", 2000);
  sendATExpect("AT+CMEE=2", "OK", 2000);

  checkGSMBattery();
  checkGSMSIM();

  bool reg = false;
  for (int i = 0; i < 10 && !reg; i++) {
    reg = checkGSMNetwork();
    if (!reg) delay(3000);
  }

  if (reg) {
    String csq = sendAT("AT+CSQ");
    int idx = csq.indexOf("+CSQ:");
    if (idx != -1) gsmRSSI = csq.substring(idx + 5).toInt();
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
//  WebServer — JSON data endpoint + dashboard
// ════════════════════════════════════════════════════════════════

void handleData(AsyncWebServerRequest* req) {
  const char* tdsQ =
    tdsValue < 50  ? "Excellent" :
    tdsValue < 150 ? "Good"      :
    tdsValue < 300 ? "Fair"      :
    tdsValue < 600 ? "Poor"      : "Bad";

  char buf[768];
  int n = snprintf(buf, sizeof(buf),
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
  // Guard: ensure null-terminated and valid JSON
  buf[sizeof(buf) - 1] = '\0';
  if (n < 0) {
    req->send(500, "application/json", "{\"error\":\"json buffer overflow\"}");
    return;
  }
  req->send(200, "application/json", buf);
}

void handleRoot(AsyncWebServerRequest* req) {
  static const char dashboard[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Multi-Layer Aquaponic System</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    :root {
      --bg:        #f0f4f8;
      --surface:   #ffffff;
      --surface2:  #e8edf2;
      --border:    #d0d8e4;
      --text:      #1a2332;
      --muted:     #5a6a80;
      --good:      #2e7d32;
      --warn:      #e65100;
      --bad:       #c62828;
      --accent:    #1565c0;
      --purple:    #6a1b9a;
      --teal:      #00695c;
      --radius:    10px;
      --font:      'Segoe UI', Arial, sans-serif;
    }
    body {
      font-family: var(--font);
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
    }
    .header {
      background: var(--accent);
      border-bottom: 3px solid #0d47a1;
      padding: 20px 24px 16px;
      display: flex;
      flex-direction: column;
      gap: 6px;
    }
    .header .title {
      font-size: 26px;
      font-weight: 800;
      color: #ffffff;
      letter-spacing: 0.2px;
      line-height: 1.2;
    }
    .header .subtitle {
      font-size: 12px;
      color: #bbdefb;
    }
    .header .devs {
      font-size: 11px;
      color: #90caf9;
      margin-top: 2px;
    }
    .statusbar {
      display: flex;
      align-items: center;
      gap: 16px;
      padding: 7px 20px;
      background: var(--surface);
      border-bottom: 1px solid var(--border);
      font-size: 11px;
      flex-wrap: wrap;
    }
    .status-dot {
      width: 8px; height: 8px;
      border-radius: 50%;
      display: inline-block;
      margin-right: 5px;
      vertical-align: middle;
    }
    .dot-on  { background: var(--good);  box-shadow: 0 0 6px var(--good); }
    .dot-off { background: #b0bec5; }
    .dot-warn{ background: var(--warn);  box-shadow: 0 0 6px var(--warn); }
    .dot-bad { background: var(--bad);   box-shadow: 0 0 6px var(--bad); }
    #last-update { margin-left: auto; color: var(--muted); }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(190px, 1fr));
      gap: 12px;
      padding: 16px 20px;
    }
    .card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 14px 16px;
      position: relative;
      transition: border-color 0.3s;
    }
    .card.alert  { border-color: var(--bad); }
    .card.warn   { border-color: var(--warn); }
    .card.normal { border-color: var(--good); }
    .card-icon {
      font-size: 20px;
      margin-bottom: 6px;
      display: block;
    }
    .card-label {
      font-size: 10px;
      font-weight: 600;
      letter-spacing: 1px;
      text-transform: uppercase;
      color: var(--muted);
      margin-bottom: 4px;
    }
    .card-value {
      font-size: 26px;
      font-weight: 700;
      line-height: 1;
      color: var(--text);
      transition: color 0.3s;
    }
    .card-unit {
      font-size: 12px;
      font-weight: 400;
      color: var(--muted);
      margin-left: 3px;
    }
    .card-sub {
      font-size: 11px;
      color: var(--muted);
      margin-top: 5px;
    }
    .relay-badge {
      display: inline-block;
      padding: 3px 9px;
      border-radius: 20px;
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.5px;
    }
    .relay-on  { background: #e8f5e9; color: var(--good); border: 1px solid var(--good); }
    .relay-off { background: #f5f5f5; color: #78909c; border: 1px solid #b0bec5; }
    .progress-wrap {
      background: #e0e0e0;
      border-radius: 4px;
      height: 5px;
      margin-top: 7px;
      overflow: hidden;
    }
    .progress-bar {
      height: 100%;
      border-radius: 4px;
      transition: width 0.5s, background 0.5s;
    }
    .section-title {
      font-size: 10px;
      font-weight: 700;
      letter-spacing: 1.5px;
      text-transform: uppercase;
      color: var(--muted);
      padding: 4px 20px 0;
    }
    .footer {
      text-align: center;
      padding: 12px 20px;
      font-size: 11px;
      color: var(--muted);
      border-top: 1px solid var(--border);
      background: var(--surface);
    }
    @keyframes pulse {
      0%,100% { opacity: 1; }
      50%      { opacity: 0.6; }
    }
    .pulsing { animation: pulse 1.5s ease-in-out infinite; }
  </style>
</head>
<body>

<div class="header">
  <div class="title">&#127754; Multi-Layer Aquaponic for Crayfish and Fish (Zebra Danios) System</div>
  <div class="subtitle">ESP32 Real-Time Monitoring Dashboard &mdash; Auto-refresh every 2s</div>
  <div class="devs">
    Acosta, Mark Girone C. &nbsp;|&nbsp;
    Antoniano, Ryan Russel A. &nbsp;|&nbsp;
    Bumatay, Axel Jillian C. &nbsp;|&nbsp;
    Cruz, Hanna Clerdee E. &nbsp;|&nbsp;
    Danque, John Michael G. &nbsp;|&nbsp;
    Yebes, Kim Jensen B.
  </div>
</div>

<div class="statusbar">
  <span><span class="status-dot dot-off" id="sb-pump-dot"></span><span id="sb-pump">Pump OFF</span></span>
  <span><span class="status-dot dot-off" id="sb-uv-dot"></span><span id="sb-uv">UV OFF</span></span>
  <span><span class="status-dot dot-off" id="sb-air-dot"></span><span id="sb-air">Air Relay OFF</span></span>
  <span><span class="status-dot dot-off" id="sb-wpump-dot"></span><span id="sb-wpump">Water Pump OFF</span></span>
  <span><span class="status-dot dot-off" id="sb-trelay-dot"></span><span id="sb-trelay">Temp Relay OFF</span></span>
  <span><span class="status-dot dot-off" id="sb-gsm-dot"></span><span id="sb-gsm">GSM —</span></span>
  <span id="last-update">Last update: —</span>
</div>

<!-- Water Quality Section -->
<div class="section-title">&#128167; Water Quality</div>
<div class="grid">

  <div class="card" id="card-tds">
    <span class="card-icon">&#9878;</span>
    <div class="card-label">TDS</div>
    <div class="card-value" id="tds">—<span class="card-unit">ppm</span></div>
    <div class="card-sub" id="tds-quality">—</div>
  </div>

  <div class="card" id="card-ph">
    <span class="card-icon">&#9878;</span>
    <div class="card-label">pH Level</div>
    <div class="card-value" id="ph">—</div>
    <div class="card-sub" id="ph-status">—</div>
  </div>

  <div class="card" id="card-temp">
    <span class="card-icon">&#127777;&#65039;</span>
    <div class="card-label">Water Temperature</div>
    <div class="card-value" id="temp">—<span class="card-unit">&deg;C</span></div>
    <div class="card-sub" id="temp-sub">DS18B20</div>
  </div>

  <div class="card" id="card-lux">
    <span class="card-icon">&#128161;</span>
    <div class="card-label">Light Intensity</div>
    <div class="card-value" id="lux">—<span class="card-unit">lux</span></div>
    <div class="card-sub" id="lux-sub">BH1750</div>
  </div>

</div>

<!-- Water Level Section -->
<div class="section-title">&#128704; Water Level (Ultrasonic)</div>
<div class="grid">

  <div class="card" id="card-sonar">
    <span class="card-icon">&#128225;</span>
    <div class="card-label">Distance (Sensor to Water)</div>
    <div class="card-value" id="sonar-dist">—<span class="card-unit">cm</span></div>
    <div class="card-sub" id="sonar-sub">AJ-SR04M</div>
  </div>

  <div class="card" id="card-wlevel">
    <span class="card-icon">&#128167;</span>
    <div class="card-label">Water Level (from bottom)</div>
    <div class="card-value" id="water-level">—<span class="card-unit">cm</span></div>
    <div class="card-sub" id="wlevel-sub">Target: 10.0 cm</div>
  </div>

</div>

<!-- Actuators Section -->
<div class="section-title">&#9881;&#65039; Actuators</div>
<div class="grid">

  <div class="card">
    <span class="card-icon">&#128166;</span>
    <div class="card-label">Flush Pump</div>
    <div style="margin-top:6px"><span class="relay-badge relay-off" id="pump-badge">OFF</span></div>
    <div class="card-sub" style="margin-top:8px">Triggers above <strong>300 ppm</strong> TDS</div>
  </div>

  <div class="card">
    <span class="card-icon">&#9728;&#65039;</span>
    <div class="card-label">UV Lamp</div>
    <div style="margin-top:6px"><span class="relay-badge relay-off" id="uv-badge">OFF</span></div>
    <div class="card-sub" style="margin-top:8px">Triggers below <strong>10 lux</strong></div>
  </div>

  <div class="card">
    <span class="card-icon">&#128168;</span>
    <div class="card-label">Air Quality Relay</div>
    <div style="margin-top:6px"><span class="relay-badge relay-off" id="air-badge">OFF</span></div>
    <div class="card-sub" style="margin-top:8px">Triggers at POOR (&ge;<strong>1000 ppm</strong>)</div>
  </div>

  <div class="card">
    <span class="card-icon">&#128704;</span>
    <div class="card-label">Water Fill Pump</div>
    <div style="margin-top:6px"><span class="relay-badge relay-off" id="wpump-badge">OFF</span></div>
    <div class="card-sub" style="margin-top:8px">Triggers below <strong>10 cm</strong> water level</div>
  </div>

  <div class="card">
    <span class="card-icon">&#127777;&#65039;</span>
    <div class="card-label">Temp Control Relay</div>
    <div style="margin-top:6px"><span class="relay-badge relay-off" id="trelay-badge">OFF</span></div>
    <div class="card-sub" style="margin-top:8px">ON &ge; <strong>30&deg;C</strong> &mdash; OFF &le; <strong>28&deg;C</strong></div>
  </div>

</div>

<!-- Air Quality Section -->
<div class="section-title">&#127981; Air Quality (MQ135)</div>
<div class="grid">

  <div class="card" id="card-aq">
    <span class="card-icon">&#129331;</span>
    <div class="card-label">CO&sup2; Equivalent</div>
    <div class="card-value" id="aq-ppm">—<span class="card-unit">ppm</span></div>
    <div class="card-sub" id="aq-label">—</div>
    <div class="progress-wrap" style="margin-top:10px">
      <div class="progress-bar" id="aq-bar" style="width:100%;background:var(--good)"></div>
    </div>
    <div class="card-sub" id="aq-pct" style="margin-top:5px">Air quality: —%</div>
  </div>

</div>

<!-- GSM / Comms Section -->
<div class="section-title">&#128241; GSM / Alerts</div>
<div class="grid">

  <div class="card">
    <span class="card-icon">&#128225;</span>
    <div class="card-label">GSM Module</div>
    <div class="card-value" id="gsm-ready" style="font-size:16px;margin-top:4px">—</div>
    <div class="card-sub" id="gsm-rssi">RSSI: —</div>
    <div class="card-sub" id="gsm-volt">Voltage: —</div>
  </div>

  <div class="card">
    <span class="card-icon">&#128140;</span>
    <div class="card-label">SMS Alerts Sent</div>
    <div class="card-value" id="sms-sent">0</div>
    <div class="card-sub">pH out-of-range alerts</div>
  </div>

</div>

<div class="footer">
  Multi-Layer Aquaponic for Crayfish and Fish (Zebra Danios) System &mdash; ESP32 &bull; Data endpoint: /data
</div>

<script>
// ── Color helpers ──────────────────────────────────────────────
function tdsColor(v) {
  if (v < 150) return 'var(--good)';
  if (v < 300) return 'var(--warn)';
  return 'var(--bad)';
}
function phColor(v) {
  if (v >= 6.0 && v <= 8.0) return 'var(--good)';
  if (v >= 5.5 && v <= 8.5) return 'var(--warn)';
  return 'var(--bad)';
}
function tempColor(v) {
  if (v >= 18 && v <= 26) return 'var(--good)';
  if (v >= 15 && v <= 28) return 'var(--warn)';
  return 'var(--bad)';
}
function luxColor(v) {
  if (v > 10) return 'var(--good)';
  if (v > 5)  return 'var(--warn)';
  return 'var(--bad)';
}
function aqColor(label) {
  if (label === 'GOOD')     return 'var(--good)';
  if (label === 'MODERATE') return 'var(--teal)';
  if (label === 'POOR')     return 'var(--warn)';
  if (label === 'BAD')      return 'var(--bad)';
  return '#b71c1c';
}
function wlevelColor(v) {
  if (v < 0)  return 'var(--muted)';
  if (v >= 10) return 'var(--good)';
  if (v >= 5)  return 'var(--warn)';
  return 'var(--bad)';
}
function cardState(color) {
  if (color === 'var(--good)') return 'normal';
  if (color === 'var(--warn)') return 'warn';
  if (color === 'var(--muted)') return '';
  return 'alert';
}
function setDot(dot, state) {
  dot.className = 'status-dot dot-' + state;
}
function setRelay(badgeId, on) {
  const el = document.getElementById(badgeId);
  if (!el) return;
  el.textContent = on ? 'ON' : 'OFF';
  el.className   = 'relay-badge ' + (on ? 'relay-on pulsing' : 'relay-off');
}

// ── Thresholds mirrored from firmware ──────────────────────────
const AQ_GOOD_JS = 400;
const AQ_POOR_JS = 1000;

// ── Fetch & render ─────────────────────────────────────────────
async function fetchData() {
  try {
    const res = await fetch('/data');
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const d = await res.json();

    // ── TDS ──────────────────────────────────────────────────
    const tdsC = tdsColor(d.tds);
    document.getElementById('tds').firstChild.textContent = d.tds.toFixed(1);
    document.getElementById('tds').style.color = tdsC;
    document.getElementById('tds-quality').textContent = d.tds_quality || '';
    document.getElementById('card-tds').className = 'card ' + cardState(tdsC);

    // ── pH ───────────────────────────────────────────────────
    const phC = phColor(d.ph);
    document.getElementById('ph').textContent = d.ph.toFixed(2);
    document.getElementById('ph').style.color = phC;
    const phOOR = (d.ph < 6.0 || d.ph > 8.0);
    document.getElementById('ph-status').textContent = phOOR ? '\u26a0\ufe0f OUT OF RANGE' : '\u2713 Normal (6.0\u20138.0)';
    document.getElementById('ph-status').style.color = phC;
    document.getElementById('card-ph').className = 'card ' + cardState(phC);

    // ── Temperature ──────────────────────────────────────────
    const tC = tempColor(d.temp);
    document.getElementById('temp').firstChild.textContent = d.temp.toFixed(2);
    document.getElementById('temp').style.color = tC;
    document.getElementById('card-temp').className = 'card ' + cardState(tC);

    // ── Lux ──────────────────────────────────────────────────
    const lC = luxColor(d.lux);
    document.getElementById('lux').firstChild.textContent = d.lux.toFixed(1);
    document.getElementById('lux').style.color = lC;
    document.getElementById('lux-sub').textContent = d.lux <= 10 ? '\u26a0\ufe0f Low light \u2014 UV ON' : '\u2713 Sufficient light';
    document.getElementById('lux-sub').style.color = lC;
    document.getElementById('card-lux').className = 'card ' + cardState(lC);

    // ── Water level ──────────────────────────────────────────
    const wlC = wlevelColor(d.water_level);
    document.getElementById('sonar-dist').firstChild.textContent = d.sonar_dist >= 0 ? d.sonar_dist.toFixed(2) : '—';
    document.getElementById('sonar-dist').style.color = d.sonar_dist >= 0 ? wlC : 'var(--muted)';
    document.getElementById('sonar-sub').textContent = d.sonar_dist < 0 ? '\u26a0\ufe0f No echo' : '\u2713 Detected';
    document.getElementById('sonar-sub').style.color = d.sonar_dist < 0 ? 'var(--bad)' : 'var(--good)';

    document.getElementById('water-level').firstChild.textContent = d.water_level >= 0 ? d.water_level.toFixed(2) : '—';
    document.getElementById('water-level').style.color = wlC;
    document.getElementById('wlevel-sub').textContent = d.water_level >= 0
      ? 'Target: 10.0 cm ' + (d.water_level >= 10 ? '\u2713 OK' : '\u26a0\ufe0f Low')
      : 'Sensor error';
    document.getElementById('wlevel-sub').style.color = wlC;
    document.getElementById('card-wlevel').className = 'card ' + cardState(wlC);

    // ── Relays ───────────────────────────────────────────────
    setRelay('pump-badge',  d.pump);
    setRelay('uv-badge',    d.uv);
    setRelay('air-badge',   d.air_relay);
    setRelay('wpump-badge', d.water_pump);
    setRelay('trelay-badge',d.temp_relay);

    // ── Status bar ───────────────────────────────────────────
    setDot(document.getElementById('sb-pump-dot'),   d.pump       ? 'on'   : 'off');
    setDot(document.getElementById('sb-uv-dot'),     d.uv         ? 'warn' : 'off');
    setDot(document.getElementById('sb-air-dot'),    d.air_relay  ? 'bad'  : 'off');
    setDot(document.getElementById('sb-wpump-dot'),  d.water_pump ? 'warn' : 'off');
    setDot(document.getElementById('sb-trelay-dot'), d.temp_relay ? 'warn' : 'off');
    setDot(document.getElementById('sb-gsm-dot'),    d.gsm_ready  ? 'on'   : 'warn');

    document.getElementById('sb-pump').textContent   = 'Pump '       + (d.pump       ? 'ON' : 'OFF');
    document.getElementById('sb-uv').textContent     = 'UV '         + (d.uv         ? 'ON' : 'OFF');
    document.getElementById('sb-air').textContent    = 'Air Relay '  + (d.air_relay  ? 'ON' : 'OFF');
    document.getElementById('sb-wpump').textContent  = 'Water Pump ' + (d.water_pump ? 'ON' : 'OFF');
    document.getElementById('sb-trelay').textContent = 'Temp Relay ' + (d.temp_relay ? 'ON' : 'OFF');
    document.getElementById('sb-gsm').textContent    = 'GSM ' + (d.gsm_ready ? 'Ready' : 'Not Ready');

    // ── Air quality (MQ135) ──────────────────────────────────
    const aqC = aqColor(d.aq_label);
    document.getElementById('aq-ppm').firstChild.textContent = d.aq_ppm.toFixed(1);
    document.getElementById('aq-ppm').style.color = aqC;
    document.getElementById('aq-label').textContent = d.aq_label;
    document.getElementById('aq-label').style.color = aqC;
    document.getElementById('aq-pct').textContent   = 'Air quality: ' + d.aq_pct.toFixed(1) + '%';
    const bar = document.getElementById('aq-bar');
    bar.style.width      = d.aq_pct.toFixed(1) + '%';
    bar.style.background = aqC;
    document.getElementById('card-aq').className = 'card ' +
      (d.aq_ppm < AQ_GOOD_JS ? 'normal' : d.aq_ppm < AQ_POOR_JS ? 'warn' : 'alert');

    // ── GSM ──────────────────────────────────────────────────
    const gsmEl = document.getElementById('gsm-ready');
    gsmEl.textContent  = d.gsm_ready ? '\u2705 Ready' : '\u274c Not Ready';
    gsmEl.style.color  = d.gsm_ready ? 'var(--good)' : 'var(--bad)';
    document.getElementById('gsm-rssi').textContent  = 'RSSI: ' + (d.rssi || 0) +
      (d.rssi <= 9 ? ' \u26a0\ufe0f Weak' : d.rssi <= 14 ? ' \u2014 Fair' : ' \u2014 OK');
    document.getElementById('gsm-volt').textContent  = 'Voltage: ' + (d.gsm_volt ? (d.gsm_volt / 1000.0).toFixed(2) + ' V' : '—');
    document.getElementById('sms-sent').textContent = d.sms_sent;

    // ── Timestamp ────────────────────────────────────────────
    document.getElementById('last-update').textContent =
      'Last update: ' + new Date().toLocaleTimeString();

  } catch(e) {
    console.error('Fetch error:', e);
    document.getElementById('last-update').textContent = '\u26a0\ufe0f Connection lost';
  }
}

fetchData();
setInterval(fetchData, 2000);
</script>
</body>
</html>
)rawliteral";
  req->send_P(200, "text/html", dashboard);
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

  // I²C + BH1750
  Wire.begin(21, 22);
  Wire.setClock(50000);   // 50 kHz — stable with long jumper wires
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23);

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

  // GSM init (auto-baud — blocks while scanning)
  GSM_SERIAL.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);
  gsmReady = initGSM();

  // Wait up to 10s for WiFi
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long wt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wt < 10000) delay(500);
  }

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.on("/",     HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.begin();
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
// ════════════════════════════════════════════════════════════════

void loop() {
  static unsigned long tSample = 0, tTDS = 0, tLux = 0,
                       tTemp   = 0, tPH  = 0, tAQ  = 0, tSonar = 0;
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
    lastLux = lightMeter.readLightLevel();
    setUV(lastLux <= LUX_THRESH);
  }

  // ── Timer 4 · DS18B20 async read + temp relay (2000 ms) ──────
  if (now - tTemp >= 2000) {
    tTemp = now;
    float t = tempSensor.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) waterTemp = t;
    tempSensor.requestTemperatures();

    if (waterTemp >= TEMP_HIGH)       setTempRelay(true);
    else if (waterTemp <= TEMP_LOW)   setTempRelay(false);
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

    bool oor = (phValue < PH_LOW || phValue > PH_HIGH);

    if (oor && !wasOutOfRange) {
      outOfRangeStart = now;
      wasOutOfRange   = true;
    } else if (!oor && wasOutOfRange) {
      wasOutOfRange = false;
      lastAlertTime = 0;
    }

    if (gsmReady && oor) {
      bool first   = (lastAlertTime == 0);
      bool elapsed = (now - lastAlertTime >= ALERT_INTERVAL);
      if (first || elapsed) {
        sendSMS(phValue);
        lastAlertTime = now;
        String csq = sendAT("AT+CSQ");
        int ci = csq.indexOf("+CSQ:");
        if (ci != -1) gsmRSSI = csq.substring(ci + 5).toInt();
      }
    }
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
}
