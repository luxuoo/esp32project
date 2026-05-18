#include "globals.h"
#include "config.h"
#include "sensors.h"
#include "display.h"
#include "weather.h"
#include "mqtt_handler.h"
#include "d6_blink.h"

// ==================== 中断（必须在 main 中，IRAM_ATTR）====================
void IRAM_ATTR buttonISR() {
  flag_button_refresh = true;
}
void IRAM_ATTR timerISR() {
  flag_timer_read = true;
}

// ==================== I2C 扫描 ====================
void scanI2C() {
  Serial.println("[I2C] 扫描...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] 设备: 0x%02X\n", addr);
    }
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========== ESP32 启动 ==========");

  pinMode(PIN_D3, OUTPUT); pinMode(PIN_D4, OUTPUT); pinMode(PIN_D6, OUTPUT);
  pinMode(PIN_SW1, INPUT_PULLUP);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_D5, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  attachInterrupt(digitalPinToInterrupt(PIN_SW1), buttonISR, FALLING);
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

  // 走路小人开机动画 3 秒
  playBootAnimation();

  // WiFi 连接
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long wifiStart    = millis();
  unsigned long lastD3Toggle = 0;
  unsigned long lastOledUp   = 0;
  int dotAnim = 0;

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastD3Toggle >= 250) {
      digitalWrite(PIN_D3, !digitalRead(PIN_D3));
      lastD3Toggle = millis();
    }
    if (millis() - lastOledUp >= 500) {
      lastOledUp = millis();
      dotAnim = (dotAnim + 1) % 4;
      int elapsed = (millis() - wifiStart) / 1000;
      int pct = constrain((int)(millis() - wifiStart) * 100 / 30000, 0, 100);

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_wqy12_t_gb2312);
      u8g2.setCursor(0, 12);
      u8g2.print("WiFi 连接中");
      for (int i = 0; i < dotAnim; i++) u8g2.print(".");
      u8g2.setCursor(0, 28);
      u8g2.print("SSID: "); u8g2.print(WIFI_SSID);
      u8g2.drawFrame(0, 38, 128, 10);
      u8g2.drawBox(1, 39, pct * 126 / 100, 8);
      u8g2.setCursor(0, 60);
      u8g2.print("耗时: "); u8g2.print(elapsed); u8g2.print("s / 30s");
      u8g2.sendBuffer();
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

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setCursor(0, 12); u8g2.print("WiFi 已连接!");
    u8g2.setCursor(0, 30); u8g2.print("IP: "); u8g2.print(WiFi.localIP().toString());
    u8g2.setCursor(0, 48); u8g2.print("信号: "); u8g2.print(WiFi.RSSI()); u8g2.print(" dBm");
    u8g2.sendBuffer();
    delay(1500);
  }

  // MQTT 连接
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);

  unsigned long lastD4Toggle = 0;
  unsigned long lastMqttOled = 0;
  int mqttRetry = 0;
  int mqttDotAnim = 0;

  while (!client.connected() && mqttRetry < 10) {
    if (millis() - lastD4Toggle >= 150) {
      digitalWrite(PIN_D4, !digitalRead(PIN_D4));
      lastD4Toggle = millis();
    }
    if (millis() - lastMqttOled >= 500) {
      lastMqttOled = millis();
      mqttDotAnim = (mqttDotAnim + 1) % 4;
      int pct = mqttRetry * 100 / 10;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_wqy12_t_gb2312);
      u8g2.setCursor(0, 12);
      u8g2.print("MQTT 连接中");
      for (int i = 0; i < mqttDotAnim; i++) u8g2.print(".");
      u8g2.setCursor(0, 28);
      u8g2.print("服务器: "); u8g2.print(MQTT_SERVER);
      u8g2.setCursor(0, 44);
      u8g2.print("重试: "); u8g2.print(mqttRetry); u8g2.print("/10");
      u8g2.drawFrame(0, 52, 128, 10);
      u8g2.drawBox(1, 53, pct * 126 / 100, 8);
      u8g2.sendBuffer();
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
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setCursor(0, 20); u8g2.print("MQTT 已连接!");
    u8g2.setCursor(0, 40); u8g2.print("主题已订阅");
    u8g2.sendBuffer();
    delay(1000);
  }

  // 首次获取天气
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, 30); u8g2.print("获取天气中...");
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

  // 按钮手动刷新
  if (flag_button_refresh) {
    flag_button_refresh = false;
    if (millis() - lastButtonPress > DEBOUNCE_MS) {
      lastButtonPress = millis();
      handleButtonRefresh();
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
