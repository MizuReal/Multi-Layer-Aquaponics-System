// ════════════════════════════════════════════════════════════════
//  MQTT — JSON builder + broker connection
//  Requires PubSubClient (included via Compilation.ino)
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
