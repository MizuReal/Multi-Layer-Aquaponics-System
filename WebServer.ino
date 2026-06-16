// ════════════════════════════════════════════════════════════════
//  WebServer — HTTP handlers (dashboard + JSON endpoint)
//  Requires ESPAsyncWebServer (included via Compilation.ino)
//  JSON builder lives in Mqtt.ino (buildSensorJSON)
// ════════════════════════════════════════════════════════════════

void handleData(AsyncWebServerRequest* req) {
  char buf[640];
  buildSensorJSON(buf, sizeof(buf));
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
