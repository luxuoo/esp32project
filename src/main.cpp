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

  // 顶栏
  u8g2.setCursor(0, 12);
  u8g2.print("D5=");
  u8g2.print(d5_brightness);
  u8g2.print("%");
  u8g2.setCursor(80, 12);
  u8g2.print("[1/2]");
  u8g2.drawHLine(0, 14, 128);

  // 传感器卡片区域（三行带图标）
  if (sensor_error) {
    // 温度图标（温度计形状）
    u8g2.drawFrame(6, 20, 3, 12);
    u8g2.drawDisc(5, 34, 3);
    u8g2.setCursor(14, 30); u8g2.print("温度: -- C");

    // 湿度图标（水滴形状）
    u8g2.drawTriangle(6, 38, 2, 44, 10, 44);
    u8g2.drawDisc(6, 45, 3);
    u8g2.setCursor(14, 45); u8g2.print("湿度: -- %RH");

    // 光照图标（太阳形状）
    u8g2.drawCircle(6, 57, 3);
    u8g2.drawLine(6, 52, 6, 50); u8g2.drawLine(6, 62, 6, 64);
    u8g2.drawLine(1, 57, 0, 57); u8g2.drawLine(11, 57, 13, 57);
    u8g2.setCursor(14, 60); u8g2.print("光照: -- 级");
  } else {
    // 温度
    u8g2.drawFrame(6, 20, 3, 12);
    u8g2.drawDisc(5, 34, 3);
    u8g2.setCursor(14, 30); u8g2.print("温度: "); u8g2.print(temp, 1); u8g2.print(" C");

    // 湿度
    u8g2.drawTriangle(6, 38, 2, 44, 10, 44);
    u8g2.drawDisc(6, 45, 3);
    u8g2.setCursor(14, 45); u8g2.print("湿度: "); u8g2.print(hum, 1); u8g2.print(" %RH");

    // 光照
    u8g2.drawCircle(6, 57, 3);
    u8g2.drawLine(6, 52, 6, 50); u8g2.drawLine(6, 62, 6, 64);
    u8g2.drawLine(1, 57, 0, 57); u8g2.drawLine(11, 57, 13, 57);
    u8g2.setCursor(14, 60); u8g2.print("光照: "); u8g2.print(light_level); u8g2.print(" 级");
  }
  u8g2.sendBuffer();
}

void drawPage2() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, 12);
  u8g2.print("[2/2] 天气信息");
  u8g2.drawHLine(0, 14, 128);

  if (weather_fetch_failed) {
    // 失败图标（X标记）
    u8g2.drawLine(50, 28, 78, 50); u8g2.drawLine(78, 28, 50, 50);
    u8g2.setCursor(32, 60); u8g2.print("获取失败");
  } else if (weather_available) {
    // ---- 左侧：大号温度 ----
    u8g2.setFont(u8g2_font_logisoso24_tf);
    u8g2.setCursor(4, 44);
    u8g2.print(weather_temp_str);

    // 度数符号
    u8g2.drawCircle(4 + weather_temp_str.length() * 14 + 2, 24, 2);

    // 单位
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(4 + weather_temp_str.length() * 14 + 6, 28);
    u8g2.print("C");

    // ---- 右侧：天气图标 + 文字 ----
    // 晴天图标（太阳）
    int iconX = 100, iconY = 32;
    u8g2.drawCircle(iconX, iconY, 6);
    u8g2.drawLine(iconX, iconY - 9, iconX, iconY - 11);
    u8g2.drawLine(iconX, iconY + 9, iconX, iconY + 11);
    u8g2.drawLine(iconX - 9, iconY, iconX - 11, iconY);
    u8g2.drawLine(iconX + 9, iconY, iconX + 11, iconY);
    u8g2.drawLine(iconX - 6, iconY - 6, iconX - 8, iconY - 8);
    u8g2.drawLine(iconX + 6, iconY - 6, iconX + 8, iconY - 8);
    u8g2.drawLine(iconX - 6, iconY + 6, iconX - 8, iconY + 8);
    u8g2.drawLine(iconX + 6, iconY + 6, iconX + 8, iconY + 8);

    // 城市 + 天气文字
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setCursor(68, 50); u8g2.print(weather_city);
    u8g2.setCursor(68, 63); u8g2.print(weather_text);

    // ---- 底部装饰线 ----
    u8g2.drawHLine(0, 63, 64);
  } else {
    // 等待动画（旋转圈）
    unsigned long t = millis();
    float angle = (t % 2000) / 2000.0 * 2 * PI;
    int cx = 64, cy = 38;
    u8g2.drawCircle(cx, cy, 8);
    int ex = cx + (int)(cos(angle) * 8);
    int ey = cy + (int)(sin(angle) * 8);
    u8g2.drawDisc(ex, ey, 2);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(40, 56); u8g2.print("Fetching...");
  }
  u8g2.sendBuffer();
}

void refreshCurrentPage() {
  if (currentPage == 1) drawPage1();
  else                  drawPage2();
}

// ==================== 星空粒子绘制 ====================
static int starX[30], starY[30], starAge[30];
static bool starsInited = false;

void drawStar(int x, int y, int age) {
  if (age % 3 == 0) { u8g2.drawPixel(x, y); return; }
  if (age % 3 == 1) { u8g2.drawPixel(x, y); u8g2.drawPixel(x + 1, y); return; }
  u8g2.drawPixel(x, y); u8g2.drawPixel(x + 1, y); u8g2.drawPixel(x, y + 1);
}

// ==================== 开机动画主函数（星空粒子）====================
void playBootAnimation() {
  unsigned long startTime = millis();

  if (!starsInited) {
    starsInited = true;
    for (int i = 0; i < 30; i++) {
      starX[i] = random(0, 128);
      starY[i] = random(0, 64);
      starAge[i] = random(0, 10);
    }
  }

  const char* loadText[] = {"INIT", "SENS", "WIFI", "MQTT", "DONE"};
  const char* title = "ESP32";
  const char* subtitle = "ENV MONITOR";
  int titleLen = 5;
  int subLen = 11;

  while (millis() - startTime < 3500) {
    unsigned long elapsed = millis() - startTime;
    float progress = (float)elapsed / 3500.0;
    int progressPct = (int)(progress * 100);

    u8g2.clearBuffer();

    // ---- 星空：漂浮星星 ----
    for (int i = 0; i < 30; i++) {
      int dx = (int)(sin(elapsed * 0.001 + i * 1.7) * 2);
      int dy = (int)(cos(elapsed * 0.0008 + i * 2.3) * 1);
      drawStar(starX[i] + dx, starY[i] + dy, starAge[i] + (int)(elapsed / 500));
    }

    // ---- 波浪线（远/近两层） ----
    for (int x = 0; x < 128; x++) {
      int y1 = 50 + (int)(sin(x * 0.05 + elapsed * 0.003) * 3);
      int y2 = 53 + (int)(sin(x * 0.07 + elapsed * 0.002 + 1.5) * 2);
      u8g2.drawPixel(x, y1);
      if (x % 2 == 0) u8g2.drawPixel(x, y2);
    }

    // ---- 标题：逐字淡入 ----
    int titleChars = min((int)(elapsed / 80), titleLen);
    u8g2.setFont(u8g2_font_6x10_tf);
    for (int i = 0; i < titleChars; i++) {
      u8g2.setCursor(44 + i * 6, 16);
      u8g2.print(title[i]);
    }
    if (titleChars < titleLen && (elapsed / 200) % 2 == 0) {
      u8g2.setCursor(44 + titleChars * 6, 16);
      u8g2.print("_");
    }

    // ---- 副标题 ----
    if (elapsed > 600) {
      int subChars = min((int)((elapsed - 600) / 60), subLen);
      u8g2.setFont(u8g2_font_5x7_tf);
      for (int i = 0; i < subChars; i++) {
        u8g2.setCursor(26 + i * 5, 26);
        u8g2.print(subtitle[i]);
      }
    }

    // ---- 加载阶段（两列布局） ----
    if (elapsed > 800) {
      int stage = min((int)((elapsed - 800) / 540), 4);
      u8g2.setFont(u8g2_font_5x7_tf);
      for (int s = 0; s <= stage && s < 5; s++) {
        int col = s < 3 ? 0 : 1;
        int row = s < 3 ? s : s - 3;
        int sx = 8 + col * 62;
        int sy = 36 + row * 8;
        u8g2.setCursor(sx, sy);
        if (s < stage) {
          u8g2.print(loadText[s]); u8g2.print(" OK");
        } else if (s == stage) {
          if ((elapsed / 200) % 2 == 0) u8g2.print(loadText[s]);
        }
      }
    }

    // ---- 底部：进度条 ----
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(0, 64);
    u8g2.print("[");
    int blocks = progressPct / 5;
    for (int i = 0; i < 20; i++) {
      u8g2.print(i < blocks ? "#" : ".");
    }
    u8g2.print("]");
    u8g2.setCursor(92, 64);
    u8g2.print(progressPct);
    u8g2.print("%");

    u8g2.sendBuffer();
  }
}

// ==================== 按钮处理（需求6）====================
void handleButtonRefresh() {
  Serial.println("[BTN] 手动刷新所有数据");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  // 循环箭头图标
  int rcx = 64, rcy = 24;
  u8g2.drawCircle(rcx, rcy, 10);
  // 箭头头部
  u8g2.drawLine(rcx + 8, rcy - 6, rcx + 12, rcy - 2);
  u8g2.drawLine(rcx + 8, rcy - 6, rcx + 4, rcy - 4);
  u8g2.setCursor(36, 44); u8g2.print("REFRESHING");
  u8g2.setCursor(44, 56); u8g2.print("...");
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
      int blocks = pct / 5;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tf);

      // 信号波动画（三层弧线扩散）
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

      // 右侧文字区
      u8g2.setCursor(56, 12); u8g2.print("WiFi");
      u8g2.setCursor(56, 22); u8g2.print("Connecting");
      // 动态省略号
      for (int d = 0; d < (dotAnim % 4); d++) u8g2.print(".");
      u8g2.setCursor(56, 34); u8g2.print(ssid);

      // 底部进度条（方块风格）
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

    // 左侧：大号勾号
    u8g2.drawLine(12, 32, 20, 42); u8g2.drawLine(20, 42, 38, 18);

    // 右侧：WiFi 信息卡片
    u8g2.setCursor(48, 12); u8g2.print("WiFi OK");
    // 信号条
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

      // 左侧：六边形网络图标
      int hx = 24, hy = 32;
      u8g2.drawLine(hx - 8, hy - 4, hx, hy - 8);
      u8g2.drawLine(hx, hy - 8, hx + 8, hy - 4);
      u8g2.drawLine(hx + 8, hy - 4, hx + 8, hy + 4);
      u8g2.drawLine(hx + 8, hy + 4, hx, hy + 8);
      u8g2.drawLine(hx, hy + 8, hx - 8, hy + 4);
      u8g2.drawLine(hx - 8, hy + 4, hx - 8, hy - 4);
      // 中心脉冲点
      if (mqttDotAnim % 2 == 0) u8g2.drawDisc(hx, hy, 2);
      else                      u8g2.drawCircle(hx, hy, 2);
      // 连接线
      u8g2.drawLine(hx + 8, hy, hx + 16, hy);
      u8g2.drawLine(hx + 16, hy - 4, hx + 16, hy + 4);

      // 右侧文字
      u8g2.setCursor(50, 12); u8g2.print("MQTT");
      u8g2.setCursor(50, 24); u8g2.print("Connecting");
      for (int d = 0; d < (mqttDotAnim % 4); d++) u8g2.print(".");
      u8g2.setCursor(50, 36); u8g2.print(mqtt_server);

      // 底部进度
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

    // 左侧：勾号
    u8g2.drawLine(12, 32, 20, 42); u8g2.drawLine(20, 42, 38, 18);

    // 右侧：订阅主题列表
    u8g2.setCursor(48, 12); u8g2.print("MQTT OK");
    // 三个主题用圆角框包裹
    u8g2.drawFrame(46, 20, 78, 40);
    u8g2.setCursor(50, 30); u8g2.print("> sensor");
    u8g2.setCursor(50, 40); u8g2.print("> weather");
    u8g2.setCursor(50, 50); u8g2.print("> led");

    u8g2.sendBuffer();
    delay(1000);
  }

  // 首次获取天气（旋转地球动画）
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  // 地球轮廓
  int gcx = 32, gcy = 32;
  u8g2.drawCircle(gcx, gcy, 14);
  u8g2.drawEllipse(gcx, gcy, 6, 14);
  // 经纬线
  u8g2.drawHLine(gcx - 12, gcy, 24);
  u8g2.drawVLine(gcx, gcy - 14, 28);
  // 右侧文字
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
