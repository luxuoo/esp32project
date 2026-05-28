#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <Wire.h>

// ==================== 前向声明（跨模块函数依赖）====================
void startD6Blink(int times);
void drawPage2();
void playBootAnimation();
void drawPage1();
void refreshCurrentPage();
void readSensors();
void publishSensorData();
bool fetchWeatherFromAPI();

// ==================== 引脚与网络 ====================
const char* ssid       = "旭的iPhone Air";
const char* password   = "123456789";
const char* mqtt_server = "123.207.45.73";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "admin";
const char* mqtt_pass   = "Lu20050910";

const int D3  = 14;
const int D4  = 27;
const int D5  = 26;
const int D6  = 33;
const int SW1 = 32;

const int OLED_SDA = 21;
const int OLED_SCL = 22;

#define DHTPIN  4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
const int GM31_PIN = 35;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA
);

WiFiClient espClient;
PubSubClient client(espClient);
WiFiClientSecure secureClient;

const char* WEATHER_API_URL =
  "https://api.open-meteo.com/v1/forecast"
  "?latitude=39.9042&longitude=116.4074"
  "&current_weather=true&timezone=Asia/Shanghai";

// ==================== 全局状态 ====================
int currentPage   = 1;
int d5_brightness = 0;
float temp = 0.0, hum = 0.0;
int light_level   = 0;
bool sensor_error = false;

String weather_city         = "北京";
String weather_text         = "--";
String weather_temp_str     = "--";
bool   weather_available    = false;
bool   weather_fetch_failed = false;

unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_INTERVAL = 600000;

volatile bool flag_button_refresh = false;
volatile bool flag_timer_read     = false;
hw_timer_t* timer = NULL;

unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

bool d6_blinking        = false;
int  d6_blink_remaining = 0;
bool d6_blink_state     = false;
unsigned long lastD6BlinkTime = 0;

unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_MS = 200;

bool wifi_was_connected = false;
bool mqtt_was_connected = false;

// ==================== 中断 ====================
void IRAM_ATTR buttonISR() {
  flag_button_refresh = true;
}
void IRAM_ATTR timerISR() {
  flag_timer_read = true;
}

// ==================== 模块包含 ====================
#include "sensors.h"
#include "weather.h"
#include "display.h"
#include "mqtt_handler.h"

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========== ESP32 启动 ==========");

  pinMode(D3, OUTPUT); pinMode(D4, OUTPUT); pinMode(D6, OUTPUT);
  pinMode(SW1, INPUT_PULLUP);

  ledcSetup(2, 5000, 8);
  ledcAttachPin(D5, 2);
  ledcWrite(2, 0);

  attachInterrupt(digitalPinToInterrupt(SW1), buttonISR, FALLING);
  timer = timerBegin(2, 80, true);
  timerAttachInterrupt(timer, &timerISR, true);
  timerAlarmWrite(timer, 5000000, true);
  timerAlarmEnable(timer);

  dht.begin();

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  scanI2C();
  u8g2.setBusClock(100000);
  u8g2.begin();
  u8g2.enableUTF8Print();

  // ========== 开机动画 3.5秒 ==========
  playBootAnimation();

  // ========== WiFi 连接 + 实时进度 ==========
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  unsigned long wifiStart    = millis();
  unsigned long lastD3Toggle = 0;
  unsigned long lastOledUp   = 0;
  int dotAnim = 0;

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastD3Toggle >= 250) {
      digitalWrite(D3, !digitalRead(D3));
      lastD3Toggle = millis();
    }
    if (millis() - lastOledUp >= 500) {
      lastOledUp = millis();
      dotAnim = (dotAnim + 1) % 4;
      int elapsed = (millis() - wifiStart) / 1000;
      int pct = constrain((int)(millis() - wifiStart) * 100 / 30000, 0, 100);
      int blocks = pct / 5;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tf);

      // 信号波动画
      int cx = 28, cy = 36;
      for (int r = 0; r < 3; r++) {
        int radius = 8 + r * 7;
        int phase = (dotAnim + r) % 4;
        if (phase < 3) {
          int startA = -40 + phase * 10;
          int endA = 40 - phase * 10;
          for (int a = startA; a <= endA; a += 5) {
            float rad = a * PI / 180.0;
            int px = cx + (int)(cos(rad) * radius);
            int py = cy - (int)(sin(rad) * radius);
            u8g2.drawPixel(px, py);
          }
        }
      }
      u8g2.drawDisc(cx, cy, 4);

      u8g2.setCursor(56, 12); u8g2.print("WiFi");
      u8g2.setCursor(56, 22); u8g2.print("Connecting");
      for (int d = 0; d < (dotAnim % 4); d++) u8g2.print(".");
      u8g2.setCursor(56, 34); u8g2.print(ssid);

      u8g2.setCursor(4, 50);
      for (int i = 0; i < 24; i++) {
        u8g2.print(i < blocks ? "\xDB" : ".");
      }
      u8g2.setCursor(4, 60);
      u8g2.print(elapsed); u8g2.print("s / 30s");
      u8g2.setCursor(90, 60); u8g2.print(pct); u8g2.print("%");

      u8g2.sendBuffer();
    }
    if (millis() - wifiStart > 30000) {
      Serial.println("[WiFi] 超时");
      break;
    }
    delay(10);
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(D3, HIGH);
    wifi_was_connected = true;
    Serial.printf("[WiFi] IP=%s\n", WiFi.localIP().toString().c_str());

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);
    int rssi = WiFi.RSSI();
    int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : 1;

    u8g2.drawLine(12, 32, 20, 42); u8g2.drawLine(20, 42, 38, 18);

    u8g2.setCursor(48, 12); u8g2.print("WiFi OK");
    for (int i = 0; i < 4; i++) {
      int bh = 2 + i * 2;
      if (i < bars) u8g2.drawBox(108 + i * 5, 8 - bh, 3, bh);
      else          u8g2.drawFrame(108 + i * 5, 8 - bh, 3, bh);
    }
    u8g2.setCursor(48, 26); u8g2.print("IP:");
    u8g2.print(WiFi.localIP().toString());
    u8g2.setCursor(48, 38); u8g2.print("RSSI: ");
    u8g2.print(rssi); u8g2.print("dBm");
    u8g2.setCursor(48, 50); u8g2.print("CH: ");
    u8g2.print(WiFi.channel());

    u8g2.sendBuffer();
    delay(1500);
  }

  // ========== MQTT 连接 + 实时进度 ==========
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  unsigned long lastD4Toggle = 0;
  unsigned long lastMqttOled = 0;
  int mqttRetry = 0;
  int mqttDotAnim = 0;

  while (!client.connected() && mqttRetry < 10) {
    if (millis() - lastD4Toggle >= 150) {
      digitalWrite(D4, !digitalRead(D4));
      lastD4Toggle = millis();
    }
    if (millis() - lastMqttOled >= 500) {
      lastMqttOled = millis();
      mqttDotAnim = (mqttDotAnim + 1) % 4;
      int pct = mqttRetry * 100 / 10;
      int blocks = pct / 5;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tf);

      int hx = 24, hy = 32;
      u8g2.drawLine(hx - 8, hy - 4, hx, hy - 8);
      u8g2.drawLine(hx, hy - 8, hx + 8, hy - 4);
      u8g2.drawLine(hx + 8, hy - 4, hx + 8, hy + 4);
      u8g2.drawLine(hx + 8, hy + 4, hx, hy + 8);
      u8g2.drawLine(hx, hy + 8, hx - 8, hy + 4);
      u8g2.drawLine(hx - 8, hy + 4, hx - 8, hy - 4);
      if (mqttDotAnim % 2 == 0) u8g2.drawDisc(hx, hy, 2);
      else                      u8g2.drawCircle(hx, hy, 2);
      u8g2.drawLine(hx + 8, hy, hx + 16, hy);
      u8g2.drawLine(hx + 16, hy - 4, hx + 16, hy + 4);

      u8g2.setCursor(50, 12); u8g2.print("MQTT");
      u8g2.setCursor(50, 24); u8g2.print("Connecting");
      for (int d = 0; d < (mqttDotAnim % 4); d++) u8g2.print(".");
      u8g2.setCursor(50, 36); u8g2.print(mqtt_server);

      u8g2.setCursor(4, 50);
      for (int i = 0; i < 24; i++) {
        u8g2.print(i < blocks ? "\xDB" : ".");
      }
      u8g2.setCursor(4, 60);
      u8g2.print("Retry "); u8g2.print(mqttRetry); u8g2.print("/10");
      u8g2.setCursor(90, 60); u8g2.print(pct); u8g2.print("%");

      u8g2.sendBuffer();
    }

    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      digitalWrite(D4, HIGH);
      mqtt_was_connected = true;
      client.subscribe("esp32/req/sensor");
      client.subscribe("esp32/req/weather");
      client.subscribe("esp32/ctrl/led");
      Serial.println("[MQTT] 已连接");
    } else {
      mqttRetry++;
      Serial.printf("[MQTT] 失败 rc=%d 重试 %d/10\n", client.state(), mqttRetry);
      delay(1000);
    }
  }

  if (client.connected()) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawLine(12, 32, 20, 42); u8g2.drawLine(20, 42, 38, 18);

    u8g2.setCursor(48, 12); u8g2.print("MQTT OK");
    u8g2.drawFrame(46, 20, 78, 40);
    u8g2.setCursor(50, 30); u8g2.print("> sensor");
    u8g2.setCursor(50, 40); u8g2.print("> weather");
    u8g2.setCursor(50, 50); u8g2.print("> led");

    u8g2.sendBuffer();
    delay(1000);
  }

  // 首次获取天气
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  int gcx = 32, gcy = 32;
  u8g2.drawCircle(gcx, gcy, 14);
  u8g2.drawEllipse(gcx, gcy, 6, 14);
  u8g2.drawHLine(gcx - 12, gcy, 24);
  u8g2.drawVLine(gcx, gcy - 14, 28);
  u8g2.setCursor(56, 20); u8g2.print("Weather");
  u8g2.setCursor(56, 32); u8g2.print("Fetching...");
  u8g2.setCursor(56, 46); u8g2.print("Please wait");
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
  bool wifiOK = (WiFi.status() == WL_CONNECTED);

  if (wifiOK) {
    if (!wifi_was_connected) {
      digitalWrite(D3, HIGH);
      wifi_was_connected = true;
      Serial.println("[WiFi] 重连成功");
    }
  } else {
    if (wifi_was_connected) {
      digitalWrite(D3, LOW);
      wifi_was_connected = false;
      Serial.println("[WiFi] 断开");
    }
    if (millis() - lastWifiCheck > 250) {
      digitalWrite(D3, !digitalRead(D3));
      lastWifiCheck = millis();
    }
  }

  bool mqttOK = client.connected() && wifiOK;

  if (mqttOK) {
    if (!mqtt_was_connected) {
      digitalWrite(D4, HIGH);
      mqtt_was_connected = true;
      client.subscribe("esp32/req/sensor");
      client.subscribe("esp32/req/weather");
      client.subscribe("esp32/ctrl/led");
      Serial.println("[MQTT] 重连成功");
    }
    client.loop();
  } else {
    if (mqtt_was_connected) {
      digitalWrite(D4, LOW);
      mqtt_was_connected = false;
      Serial.println("[MQTT] 断开");
    }
    if (wifiOK) {
      if (millis() - lastMqttCheck > 150) {
        digitalWrite(D4, !digitalRead(D4));
        lastMqttCheck = millis();
      }
      if (millis() - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
        lastMqttReconnectAttempt = millis();
        String clientId = "ESP32Client-" + String(random(0xffff), HEX);
        client.connect(clientId.c_str(), mqtt_user, mqtt_pass);
      }
    } else {
      digitalWrite(D4, LOW);
    }
  }

  if (flag_button_refresh) {
    flag_button_refresh = false;
    if (millis() - lastButtonPress > DEBOUNCE_MS) {
      lastButtonPress = millis();
      handleButtonRefresh();
    }
  }

  if (flag_timer_read) {
    flag_timer_read = false;
    readSensors();
    if (currentPage == 1) drawPage1();
    publishSensorData();
  }

  if (millis() - lastWeatherFetch > WEATHER_INTERVAL) {
    lastWeatherFetch = millis();
    fetchWeatherFromAPI();
  }

  updateD6Blink();
  yield();
}
