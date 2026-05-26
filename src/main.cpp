#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <Wire.h>

// ==================== 前向声明 ====================
void startD6Blink(int times);
void drawPage2();
void playBootAnimation();

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

// ==================== weathercode → 中文 ====================
String weathercodeToString(int code) {
  if (code == 0)                return "晴朗";
  if (code == 1)                return "少云";
  if (code == 2)                return "多云";
  if (code == 3)                return "阴天";
  if (code == 45 || code == 48) return "起雾";
  if (code >= 51 && code <= 57) return "小雨";
  if (code >= 61 && code <= 67) return "大雨";
  if (code >= 71 && code <= 77) return "下雪";
  if (code >= 80 && code <= 84) return "阵雨";
  if (code >= 95)               return "雷暴";
  return "未知";
}

// ==================== D6 非阻塞闪烁 ====================
void startD6Blink(int times) {
  d6_blink_remaining = times * 2;
  d6_blinking = true;
  d6_blink_state = false;
  lastD6BlinkTime = millis();
}
void updateD6Blink() {
  if (!d6_blinking) return;
  if (millis() - lastD6BlinkTime >= 200) {
    d6_blink_state = !d6_blink_state;
    digitalWrite(D6, d6_blink_state ? HIGH : LOW);
    lastD6BlinkTime = millis();
    if (--d6_blink_remaining <= 0) {
      d6_blinking = false;
      if (!sensor_error) digitalWrite(D6, LOW);
    }
  }
}

// ==================== 传感器 ====================
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int raw = analogRead(GM31_PIN);
  light_level = map(raw, 0, 4095, 0, 100);

  if (isnan(t) || isnan(h)) {
    sensor_error = true;
    digitalWrite(D6, HIGH);
  } else {
    temp = t; hum = h;
    sensor_error = false;
    digitalWrite(D6, LOW);
  }
}

void publishSensorData() {
  if (sensor_error || !client.connected()) return;
  JsonDocument doc;
  doc["temp"]  = temp;
  doc["hum"]   = hum;
  doc["light"] = light_level;
  char buf[200];
  serializeJson(doc, buf);
  client.publish("esp32/resp/sensor", buf);
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

// ==================== 天气 API ====================
bool fetchWeatherFromAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Weather] WiFi 未连接");
    weather_fetch_failed = true;
    if (currentPage == 2) drawPage2();
    return false;
  }

  Serial.println("[Weather] 请求 API...");
  HTTPClient http;
  secureClient.setInsecure();
  http.begin(secureClient, WEATHER_API_URL);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[Weather] HTTP 失败: %d\n", httpCode);
    http.end();
    weather_fetch_failed = true;
    startD6Blink(3);
    if (currentPage == 2) drawPage2();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Weather] JSON 解析失败: %s\n", err.c_str());
    weather_fetch_failed = true;
    startD6Blink(3);
    if (currentPage == 2) drawPage2();
    return false;
  }

  JsonObject cw = doc["current_weather"];
  if (cw.isNull()) {
    weather_fetch_failed = true;
    startD6Blink(3);
    if (currentPage == 2) drawPage2();
    return false;
  }

  float api_temp = cw["temperature"] | 0.0f;
  int   api_code = cw["weathercode"] | -1;

  weather_city         = "北京";
  weather_text         = weathercodeToString(api_code);
  weather_temp_str     = String(api_temp, 1);
  weather_available    = true;
  weather_fetch_failed = false;

  Serial.printf("[Weather] %s %s C (code=%d)\n",
    weather_text.c_str(), weather_temp_str.c_str(), api_code);

  if (client.connected()) {
    JsonDocument resp;
    resp["city"]    = weather_city;
    resp["weather"] = weather_text;
    resp["temp"]    = weather_temp_str;
    resp["code"]    = api_code;
    char buf[256];
    serializeJson(resp, buf);
    client.publish("esp32/resp/weather", buf);
  }

  if (currentPage == 2) drawPage2();
  return true;
}

// ==================== OLED 界面 ====================
void drawPage1() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, 12);
  u8g2.print("D5=");
  u8g2.print(d5_brightness);
  u8g2.print("%");
  u8g2.setCursor(80, 12);
  u8g2.print("[1/2]");
  u8g2.drawLine(0, 14, 128, 14);

  u8g2.setCursor(0, 30);
  if (sensor_error) {
    u8g2.print("温度: -- C");
    u8g2.setCursor(0, 45); u8g2.print("湿度: -- %RH");
    u8g2.setCursor(0, 60); u8g2.print("光照: -- 级");
  } else {
    u8g2.print("温度: "); u8g2.print(temp, 1); u8g2.print(" C");
    u8g2.setCursor(0, 45);
    u8g2.print("湿度: "); u8g2.print(hum, 1); u8g2.print(" %RH");
    u8g2.setCursor(0, 60);
    u8g2.print("光照: "); u8g2.print(light_level); u8g2.print(" 级");
  }
  u8g2.sendBuffer();
}

void drawPage2() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, 12);
  u8g2.print("[2/2] 天气信息");
  u8g2.drawLine(0, 14, 128, 14);

  if (weather_fetch_failed) {
    u8g2.setCursor(0, 35);
    u8g2.print("天气获取失败");
  } else if (weather_available) {
    u8g2.setCursor(0, 30); u8g2.print("城市: "); u8g2.print(weather_city);
    u8g2.setCursor(0, 45); u8g2.print("天气: "); u8g2.print(weather_text);
    u8g2.setCursor(0, 60); u8g2.print("温度: "); u8g2.print(weather_temp_str); u8g2.print(" C");
  } else {
    u8g2.setCursor(0, 35);
    u8g2.print("正在获取天气...");
  }
  u8g2.sendBuffer();
}

void refreshCurrentPage() {
  if (currentPage == 1) drawPage1();
  else                  drawPage2();
}

// ==================== 雷达扫描绘制 ====================
// 雷达中心 cx, cy, 扫描角度 angle (弧度×100), 信号点
static int radarDots[8][2];
static bool radarDotsReady = false;

void drawRadar(int cx, int cy, float angle) {
  // 同心圆
  u8g2.drawCircle(cx, cy, 8);
  u8g2.drawCircle(cx, cy, 17);
  u8g2.drawCircle(cx, cy, 25);

  // 十字线
  u8g2.drawLine(cx - 26, cy, cx + 26, cy);
  u8g2.drawLine(cx, cy - 26, cx + 0, cy + 26);

  // 扫描线（亮）
  int sx = cx + (int)(cos(angle) * 25);
  int sy = cy - (int)(sin(angle) * 25);
  u8g2.drawLine(cx, cy, sx, sy);

  // 拖尾渐暗效果：多条短扫线
  for (int t = 1; t <= 4; t++) {
    float a = angle - t * 0.08;
    int tx = cx + (int)(cos(a) * 25);
    int ty = cy - (int)(sin(a) * 25);
    // 奇数帧跳过实现闪烁渐暗
    if (t == 1 || (t <= 3 && (int)(angle * 50) % (t + 1) == 0)) {
      u8g2.drawLine(cx, cy, tx, ty);
    }
  }

  // 信号点（随机出现在圈上）
  if (!radarDotsReady) {
    radarDotsReady = true;
    for (int i = 0; i < 8; i++) {
      radarDots[i][0] = cx + (random(-22, 22));
      radarDots[i][1] = cy + (random(-22, 22));
      // 确保在圆内
      int dx = radarDots[i][0] - cx;
      int dy = radarDots[i][1] - cy;
      if (dx * dx + dy * dy > 25 * 25) {
        radarDots[i][0] = cx + dx * 7 / 10;
        radarDots[i][1] = cy + dy * 7 / 10;
      }
    }
  }
  for (int i = 0; i < 8; i++) {
    float dotAngle = atan2(cy - radarDots[i][1], radarDots[i][0] - cx);
    if (dotAngle < 0) dotAngle += 2 * PI;
    float diff = angle - dotAngle;
    if (diff < 0) diff += 2 * PI;
    // 只有被扫到的点才亮，且随时间渐暗
    if (diff < PI * 0.6) {
      u8g2.drawPixel(radarDots[i][0], radarDots[i][1]);
      if (diff < PI * 0.15) {
        u8g2.drawPixel(radarDots[i][0] + 1, radarDots[i][1]);
      }
    }
  }
}

// ==================== 开机动画主函数 ====================
void playBootAnimation() {
  unsigned long startTime = millis();
  const int CX = 90;  // 雷达中心 x
  const int CY = 32;  // 雷达中心 y
  const char* title = "ENV STATION";
  int titleLen = 11;
  int shownChars = 0;

  // 点阵加载文字
  const char* loadText[] = {"BOOT", "SENS", "WIFI", "MQTT", "RDY!"};

  while (millis() - startTime < 3500) {
    unsigned long elapsed = millis() - startTime;
    float angle = ((float)(elapsed % 2000) / 2000.0) * 2 * PI;

    u8g2.clearBuffer();

    // ---- 左侧：逐字打出标题 ----
    shownChars = min((int)(elapsed / 100), titleLen);
    u8g2.setFont(u8g2_font_6x10_tf);
    for (int i = 0; i < shownChars; i++) {
      u8g2.setCursor(2 + i * 6, 10);
      u8g2.print(title[i]);
    }
    // 闪烁光标
    if (shownChars < titleLen && (elapsed / 200) % 2 == 0) {
      u8g2.setCursor(2 + shownChars * 6, 10);
      u8g2.print("_");
    }

    // ---- 左侧：加载阶段文字 ----
    int stage = min((int)(elapsed / 700), 4);
    for (int s = 0; s <= stage && s < 5; s++) {
      u8g2.setCursor(4, 24 + s * 10);
      if (s < stage) {
        // 已完成：显示 [OK]
        u8g2.print(">");
        u8g2.print(loadText[s]);
        u8g2.print(" OK");
      } else if (s == stage) {
        // 当前阶段：闪烁
        if ((elapsed / 150) % 2 == 0) {
          u8g2.print(">");
          u8g2.print(loadText[s]);
        }
      }
    }

    // ---- 右侧：雷达扫描 ----
    // 分隔线
    u8g2.drawLine(56, 0, 56, 64);

    drawRadar(CX, CY, angle);

    // ---- 底部：进度条 ----
    int progress = map(elapsed, 0, 3500, 0, 100);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(0, 64);
    u8g2.print("[");
    int blocks = progress / 5;
    for (int i = 0; i < 20; i++) {
      u8g2.print(i < blocks ? "#" : ".");
    }
    u8g2.print("]");
    u8g2.setCursor(92, 64);
    u8g2.print(progress);
    u8g2.print("%");

    u8g2.sendBuffer();
  }
}

// ==================== 按钮处理（需求6）====================
void handleButtonRefresh() {
  Serial.println("[BTN] 手动刷新所有数据");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.drawHLine(0, 10, 128);
  u8g2.setCursor(28, 8);
  u8g2.print("[ REFRESH ]");
  u8g2.setCursor(24, 34);
  u8g2.print("Updating...");
  u8g2.sendBuffer();

  readSensors();
  fetchWeatherFromAPI();
  refreshCurrentPage();

  if (client.connected()) {
    client.publish("esp32/req/manual", "手动刷新成功");
    Serial.println("[MQTT] 发布: 手动刷新成功");
  }
}

// ==================== MQTT 回调 ====================
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

  // ========== 需求9: 雷达扫描开机动画 3.5秒 ==========
  playBootAnimation();

  // ========== 需求1+9: WiFi 连接 + 实时进度 ==========
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  unsigned long wifiStart    = millis();
  unsigned long lastD3Toggle = 0;
  unsigned long lastOledUp   = 0;
  int dotAnim = 0;

  const char* spinner = "|/-\\";
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
      int blocks = pct / 5; // 20格

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tf);

      // 边框 + 标题栏
      u8g2.drawFrame(0, 0, 128, 64);
      u8g2.drawHLine(0, 10, 128);
      u8g2.setCursor(36, 8);
      u8g2.print("[ WIFI ]");

      // 旋转指示器 + SSID
      u8g2.setCursor(4, 21);
      u8g2.print(spinner[dotAnim]);
      u8g2.print(" Connecting...");
      u8g2.setCursor(4, 31);
      u8g2.print("SSID: ");
      u8g2.print(ssid);

      // ASCII 进度条
      u8g2.setCursor(4, 43);
      u8g2.print("[");
      for (int i = 0; i < 20; i++) {
        u8g2.print(i < blocks ? "#" : ".");
      }
      u8g2.print("]");

      // 底部信息
      u8g2.setCursor(4, 53);
      u8g2.print(elapsed);
      u8g2.print("s / 30s");
      u8g2.setCursor(80, 53);
      u8g2.print(pct);
      u8g2.print("%");

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

    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawHLine(0, 10, 128);
    u8g2.setCursor(30, 8);
    u8g2.print("[ CONNECTED ]");

    u8g2.setCursor(4, 22);
    u8g2.print("WiFi  OK");
    // 信号强度图标
    int rssi = WiFi.RSSI();
    int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : 1;
    for (int i = 0; i < 4; i++) {
      int bh = 2 + i * 2;
      if (i < bars) u8g2.drawBox(108 + i * 5, 15 - bh, 3, bh);
      else          u8g2.drawFrame(108 + i * 5, 15 - bh, 3, bh);
    }

    u8g2.setCursor(4, 34);
    u8g2.print("IP: ");
    u8g2.print(WiFi.localIP().toString());

    u8g2.setCursor(4, 46);
    u8g2.print("RSSI: ");
    u8g2.print(rssi);
    u8g2.print(" dBm");

    u8g2.setCursor(4, 58);
    u8g2.print("CH: ");
    u8g2.print(WiFi.channel());

    u8g2.sendBuffer();
    delay(1500);
  }

  // ========== 需求2+9: MQTT 连接 + 实时进度 ==========
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

      u8g2.drawFrame(0, 0, 128, 64);
      u8g2.drawHLine(0, 10, 128);
      u8g2.setCursor(34, 8);
      u8g2.print("[ MQTT ]");

      u8g2.setCursor(4, 21);
      u8g2.print("|/-\\"[mqttDotAnim]);
      u8g2.print(" Connecting...");
      u8g2.setCursor(4, 31);
      u8g2.print("Host: ");
      u8g2.print(mqtt_server);

      u8g2.setCursor(4, 43);
      u8g2.print("[");
      for (int i = 0; i < 20; i++) {
        u8g2.print(i < blocks ? "#" : ".");
      }
      u8g2.print("]");

      u8g2.setCursor(4, 53);
      u8g2.print("Retry ");
      u8g2.print(mqttRetry);
      u8g2.print("/10");
      u8g2.setCursor(80, 53);
      u8g2.print(pct);
      u8g2.print("%");

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

    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawHLine(0, 10, 128);
    u8g2.setCursor(30, 8);
    u8g2.print("[ CONNECTED ]");

    u8g2.setCursor(4, 24);
    u8g2.print("MQTT  OK");
    u8g2.setCursor(4, 38);
    u8g2.print("> sensor  topic");
    u8g2.setCursor(4, 48);
    u8g2.print("> weather topic");
    u8g2.setCursor(4, 58);
    u8g2.print("> led     topic");

    u8g2.sendBuffer();
    delay(1000);
  }

  // 首次获取天气
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.drawHLine(0, 10, 128);
  u8g2.setCursor(28, 8);
  u8g2.print("[ WEATHER ]");
  u8g2.setCursor(30, 34);
  u8g2.print("Fetching...");
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
  // 需求1: WiFi D3
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

  // 需求2: MQTT D4
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

  // 需求6: 按钮手动刷新
  if (flag_button_refresh) {
    flag_button_refresh = false;
    if (millis() - lastButtonPress > DEBOUNCE_MS) {
      lastButtonPress = millis();
      handleButtonRefresh();
    }
  }

  // 需求7: 定时器 5 秒
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
