#include "globals.h"
#include "config.h"
#include "sensors.h"
#include "display.h"
#include "weather.h"
#include "mqtt_handler.h"
#include "d6_blink.h"
#include <math.h>


void IRAM_ATTR timerISR() {
  flag_timer_read = true;
}


void scanI2C() {
  Serial.println("[I2C] 扫描...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] 设备: 0x%02X\n", addr);
    }
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("\n========== ESP32 启动 ==========");

  // 启动阶段关闭看门狗，防止等耗时操作触发重启
  disableCore0WDT();
  disableLoopWDT();

  pinMode(PIN_D3, OUTPUT); pinMode(PIN_D4, OUTPUT); pinMode(PIN_D6, OUTPUT);
  pinMode(PIN_SW1, INPUT_PULLUP);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_D5, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  timer = timerBegin(2, 80, true);
  timerAttachInterrupt(timer, &timerISR, true);
  timerAlarmWrite(timer, 5000000, true);
  timerAlarmEnable(timer);

  dht.begin();

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(100000);
  scanI2C();
  u8g2.setBusClock(100000);
  u8g2.begin();
  u8g2.enableUTF8Print();

  // 开机动画
  timerAlarmDisable(timer);
  playBootAnimation();
  timerAlarmEnable(timer);

  
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long wifiStart    = millis();
  unsigned long lastD3Toggle = 0;
  unsigned long lastOledUp   = 0;

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastD3Toggle >= 250) {
      digitalWrite(PIN_D3, !digitalRead(PIN_D3));
      lastD3Toggle = millis();
    }
    if (millis() - lastOledUp >= 500) {
      lastOledUp = millis();
      int elapsed = (millis() - wifiStart) / 1000;
      int pct = constrain((int)(millis() - wifiStart) * 100 / 30000, 0, 100);
      char detailBuf[32], statusBuf[20];
      snprintf(detailBuf, sizeof(detailBuf), "SSID: %s", WIFI_SSID);
      snprintf(statusBuf, sizeof(statusBuf), "耗时: %ds / 30s", elapsed);
      drawConnectingScreen("WiFi 连接中...", detailBuf, statusBuf, pct);
    }
    if (millis() - wifiStart > 30000) {
      Serial.println("[WiFi] 超时");
      break;
    }
    delay(10);
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_D3, HIGH);
    wifi_was_connected = true;
    Serial.printf("[WiFi] IP=%s\n", WiFi.localIP().toString().c_str());

    drawConnectedScreen("WiFi 已连接", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    delay(1500);
  }

  // MQTT 连接
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);

  unsigned long lastD4Toggle = 0;
  unsigned long lastMqttOled = 0;
  int mqttRetry = 0;

  while (!client.connected() && mqttRetry < 10) {
    if (millis() - lastD4Toggle >= 150) {
      digitalWrite(PIN_D4, !digitalRead(PIN_D4));
      lastD4Toggle = millis();
    }
    if (millis() - lastMqttOled >= 500) {
      lastMqttOled = millis();
      int pct = mqttRetry * 100 / 10;
      char detailBuf[24], statusBuf[16];
      snprintf(detailBuf, sizeof(detailBuf), "重试 %d/10", mqttRetry);
      snprintf(statusBuf, sizeof(statusBuf), "服务器: %s", MQTT_SERVER);
      drawConnectingScreen("MQTT 连接中...", detailBuf, statusBuf, pct);
    }

    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      digitalWrite(PIN_D4, HIGH);
      mqtt_was_connected = true;
      client.subscribe(TOPIC_REQ_SENSOR);
      client.subscribe(TOPIC_REQ_WEATHER);
      client.subscribe(TOPIC_CTRL_LED);
      Serial.println("[MQTT] 已连接");
    } else {
      mqttRetry++;
      Serial.printf("[MQTT] 失败 rc=%d 重试 %d/10\n", client.state(), mqttRetry);
      delay(1000);
    }
  }

  if (client.connected()) {
    drawConnectedScreen("MQTT 已连接", MQTT_SERVER, 0);
    delay(1000);
  }

  // 首次获取天气
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  {
    // 像素加载指示器
    unsigned long t = millis() / 120;
    for (int i = 0; i < 8; i++) {
      float angle = (t * 45 + i * 45) * M_PI / 180.0;
      int px = 64 + (int)(10 * cos(angle));
      int py = 24 + (int)(10 * sin(angle));
      if (i <= (t % 8)) u8g2.drawDisc(px, py, 1);
      else              u8g2.drawPixel(px, py);
    }
    u8g2.setCursor(20, 44);
    u8g2.print("正在获取天气中");
  }
  u8g2.sendBuffer();
  fetchWeatherFromAPI();
  lastWeatherFetch = millis();

  // 进入第一页
  currentPage = 1;
  readSensors();
  drawPage1();
  Serial.println("========== 初始化完成 ==========\n");
}

// ==================== LOOP ====================
unsigned long lastWifiCheck = 0;
unsigned long lastMqttCheck = 0;

void loop() {
  // WiFi D3
  bool wifiOK = (WiFi.status() == WL_CONNECTED);

  if (wifiOK) {
    if (!wifi_was_connected) {
      digitalWrite(PIN_D3, HIGH);
      wifi_was_connected = true;
      Serial.println("[WiFi] 重连成功");
    }
  } else {
    if (wifi_was_connected) {
      digitalWrite(PIN_D3, LOW);
      wifi_was_connected = false;
      Serial.println("[WiFi] 断开");
    }
    if (millis() - lastWifiCheck > 250) {
      digitalWrite(PIN_D3, !digitalRead(PIN_D3));
      lastWifiCheck = millis();
    }
  }

  // MQTT D4
  bool mqttOK = client.connected() && wifiOK;

  if (mqttOK) {
    if (!mqtt_was_connected) {
      digitalWrite(PIN_D4, HIGH);
      mqtt_was_connected = true;
      client.subscribe(TOPIC_REQ_SENSOR);
      client.subscribe(TOPIC_REQ_WEATHER);
      client.subscribe(TOPIC_CTRL_LED);
      Serial.println("[MQTT] 重连成功");
    }
    client.loop();
  } else {
    if (mqtt_was_connected) {
      digitalWrite(PIN_D4, LOW);
      mqtt_was_connected = false;
      Serial.println("[MQTT] 断开");
    }
    if (wifiOK) {
      if (millis() - lastMqttCheck > 150) {
        digitalWrite(PIN_D4, !digitalRead(PIN_D4));
        lastMqttCheck = millis();
      }
      if (millis() - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
        lastMqttReconnectAttempt = millis();
        String clientId = "ESP32Client-" + String(random(0xffff), HEX);
        client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
      }
    } else {
      digitalWrite(PIN_D4, LOW);
    }
  }

  // 按钮: 短按切换页面, 长按(>1秒)刷新数据
  {
    static bool btnWasPressed = false;
    static unsigned long btnPressStart = 0;
    bool btnPressed = (digitalRead(PIN_SW1) == LOW);

    if (btnPressed && !btnWasPressed) {
      // 按下瞬间
      btnPressStart = millis();
      btnWasPressed = true;
    }
    if (!btnPressed && btnWasPressed) {
      // 释放瞬间
      btnWasPressed = false;
      unsigned long holdMs = millis() - btnPressStart;
      if (holdMs > DEBOUNCE_MS) {
        if (holdMs >= 1000) {
          // 长按: 刷新数据
          handleButtonRefresh();
        } else {
          // 短按: 切换页面
          currentPage = (currentPage == 1) ? 2 : 1;
          refreshCurrentPage();
          Serial.printf("[BTN] 切换到第 %d 页\n", currentPage);
        }
      }
    }
  }

  // 定时器 5 秒
  if (flag_timer_read) {
    flag_timer_read = false;
    readSensors();
    if (currentPage == 1) drawPage1();
    publishSensorData();
  }

  // 天气定时 10 分钟
  if (millis() - lastWeatherFetch > WEATHER_INTERVAL) {
    lastWeatherFetch = millis();
    fetchWeatherFromAPI();
  }

  updateD6Blink();
  yield();
}
