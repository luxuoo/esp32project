#pragma once

// ==================== MQTT 消息回调 ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("[MQTT] %s -> %s\n", topic, msg.c_str());

  if (topicStr == "esp32/req/sensor") {
    readSensors();
    currentPage = 1;
    drawPage1();
    publishSensorData();
  }
  else if (topicStr == "esp32/req/weather") {
    String requestedCity = msg;
    if (requestedCity.length() == 0) requestedCity = "北京";
    Serial.printf("[Weather] 请求城市: %s\n", requestedCity.c_str());
    currentPage = 2;
    drawPage2();
    fetchWeatherFromAPI();
  }
  else if (topicStr == "esp32/ctrl/led") {
    if (msg == "LED_ON")             { ledcWrite(2, 255); d5_brightness = 100; }
    else if (msg == "LED_BRIGHT_50") { ledcWrite(2, 127); d5_brightness = 50;  }
    else if (msg == "LED_OFF")       { ledcWrite(2, 0);   d5_brightness = 0;   }
    refreshCurrentPage();
  }
}
