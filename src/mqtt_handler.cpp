#include "mqtt_handler.h"
#include "globals.h"
#include "sensors.h"
#include "display.h"
#include "weather.h"
#include <math.h>

void handleButtonRefresh() {
  Serial.println("[BTN] 手动刷新所有数据");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 刷新动画: 旋转像素指示器
  unsigned long t = millis() / 120;
  for (int i = 0; i < 8; i++) {
    float angle = (t * 45 + i * 45) * M_PI / 180.0;
    int px = 64 + (int)(10 * cos(angle));
    int py = 24 + (int)(10 * sin(angle));
    if (i <= (t % 8))
      u8g2.drawDisc(px, py, 1);
    else
      u8g2.drawPixel(px, py);
  }
  int tw = u8g2.getStrWidth("正在刷新...");
  u8g2.setCursor(64 - tw / 2, 48);
  u8g2.print("正在刷新...");
  u8g2.sendBuffer();

  readSensors();
  fetchWeatherFromAPI();
  refreshCurrentPage();

  if (client.connected()) {
    client.publish(TOPIC_REQ_MANUAL, "手动刷新成功");
    Serial.println("[MQTT] 发布: 手动刷新成功");
  }
}

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
