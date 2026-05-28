#pragma once

// ==================== MQTT 消息回调 ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("[MQTT] %s -> %s\n", topic, msg.c_str());

  if (topicStr == TOPIC_REQ_SENSOR) {
    readSensors();
    currentPage = 1;
    drawPage1();
    publishSensorData();
  }
  else if (topicStr == TOPIC_REQ_WEATHER) {
    String requestedCity = msg;
    if (requestedCity.length() == 0) requestedCity = "北京";
    Serial.printf("[Weather] 请求城市: %s\n", requestedCity.c_str());
    currentPage = 2;
    drawPage2();
    fetchWeatherFromAPI();
  }
  else if (topicStr == TOPIC_CTRL_LED) {
    if (msg == "LED_ON")             { ledcWrite(PWM_CHANNEL, 255); d5_brightness = 100; }
    else if (msg == "LED_BRIGHT_50") { ledcWrite(PWM_CHANNEL, 127); d5_brightness = 50;  }
    else if (msg == "LED_OFF")       { ledcWrite(PWM_CHANNEL, 0);   d5_brightness = 0;   }
    refreshCurrentPage();
  }
}
