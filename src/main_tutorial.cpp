/*
 * ================================================================
 *  ESP32 智能环境监测站 —— 小白讲解版
 * ================================================================
 *
 *  本文件是给完全没接触过 ESP32 / Arduino 的同学看的。
 *  每一行关键代码都有中文注释，解释"这行是干嘛的"、
 *  "为什么要这么写"、"不这么写会怎样"。
 *
 *  硬件清单：
 *    1x ESP32 开发板
 *    1x SSD1306 OLED 128x64 屏幕（I2C 接口）
 *    1x DHT11 温湿度传感器
 *    1x GM-31 光敏电阻模块
 *    4x LED（D3/D4/D5/D6）
 *    1x 按键（SW1）
 *
 *  功能概要：
 *    - 开机播放雷达扫描动画
 *    - 自动连接 WiFi（D3 闪烁表示连接中，常亮表示已连接）
 *    - 自动连接 MQTT 服务器（D4 闪烁/常亮同理）
 *    - OLED 屏幕显示温湿度、光照、天气
 *    - 按键手动刷新所有数据
 *    - 每 5 秒自动读取传感器，每 10 分钟自动更新天气
 *
 * ================================================================
 */

// ================================================================
// 第一部分：引入库文件
// ================================================================
//
// "库"就是别人写好的工具包，我们 #include 进来就能直接用。
// 就像你做饭不需要自己种菜，买现成的就行。

#include <WiFi.h>             // ESP32 专用的 WiFi 库 —— 连 WiFi 全靠它
#include <WiFiClientSecure.h> // 带 SSL 加密的网络连接（HTTPS 用的）
#include <PubSubClient.h>     // MQTT 客户端库 —— 负责和服务器"对话"
#include <ArduinoJson.h>      // JSON 解析库 —— 把服务器返回的 JSON 字符串变成能用的数据
#include <HTTPClient.h>       // HTTP 请求库 —— 用来调天气 API
#include <U8g2lib.h>          // OLED 显示屏驱动库 —— 控制屏幕画东西
#include <DHT.h>              // DHT 温湿度传感器库 —— 读取温度和湿度
#include <Wire.h>             // I2C 通信库 —— OLED 屏幕通过 I2C 协议和 ESP32 通信

// ================================================================
// 第二部分：前向声明
// ================================================================
//
// 在 C/C++ 里，函数要"先声明、后使用"。
// 如果函数定义在文件后面，但在前面被调用了，
// 就需要提前告诉编译器"这个函数存在，别慌"。
// 这就叫"前向声明"（forward declaration）。

void startD6Blink(int times);  // 声明：让 D6 LED 闪烁 N 次的函数
void drawPage2();              // 声明：绘制天气页面的函数
void playBootAnimation();      // 声明：播放开机动画的函数

// ================================================================
// 第三部分：硬件引脚和网络配置
// ================================================================
//
// 这里把所有"常量"集中定义在文件开头，
// 这样以后换 WiFi、换服务器，只需要改这里就行，
// 不用满文件到处找。

// -------- WiFi 配置 --------
// 把这里改成你自己的 WiFi 名称和密码
const char* ssid       = "旭的iPhone Air";  // WiFi 名称
const char* password   = "123456789";        // WiFi 密码

// -------- MQTT 服务器配置 --------
// MQTT 就像一个"聊天群"，ESP32 和你的手机/电脑都加入这个群，
// 然后可以互相发消息。这里配置群的地址和账号密码。
const char* mqtt_server = "123.207.45.73";   // 服务器 IP 地址
const int   mqtt_port   = 1883;              // 端口号（1883 是 MQTT 默认端口）
const char* mqtt_user   = "admin";           // 登录用户名
const char* mqtt_pass   = "Lu20050910";      // 登录密码

// -------- LED 引脚 --------
// ESP32 的 GPIO 引脚编号和板子上印的编号可能不一样，
// 这里用变量名 D3/D4/D5/D6 让代码更易读。
const int D3  = 14;   // WiFi 状态指示灯
const int D4  = 27;   // MQTT 状态指示灯
const int D5  = 26;   // 可调亮度的 LED（PWM 控制）
const int D6  = 33;   // 传感器错误 / 天气失败指示灯

// -------- 按键引脚 --------
const int SW1 = 32;   // 手动刷新按钮

// -------- OLED 屏幕引脚 --------
// I2C 通信只需要两根线：SDA（数据）和 SCL（时钟）
const int OLED_SDA = 21;
const int OLED_SCL = 22;

// -------- DHT11 温湿度传感器 --------
#define DHTPIN  4              // 数据线接在 GPIO4
#define DHTTYPE DHT11          // 传感器型号是 DHT11
DHT dht(DHTPIN, DHTTYPE);     // 创建一个 DHT 对象，以后用 dht.readTemperature() 读温度

// -------- 光敏电阻 --------
const int GM31_PIN = 35;       // 模拟输入引脚，读出来是 0~4095 的值

// -------- 创建 OLED 显示对象 --------
// U8G2 库需要创建一个"屏幕对象"，告诉它：
//   - 用什么型号的屏幕（SSD1306 128x64）
//   - 用什么通信方式（硬件 I2C）
//   - SCL 和 SDA 分别接在哪两个引脚
// 创建之后，所有画图操作都通过 u8g2.xxx() 来调用。
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0,           // 屏幕不旋转
  U8X8_PIN_NONE,     // 不用 RESET 引脚
  OLED_SCL,          // SCL 引脚
  OLED_SDA           // SDA 引脚
);

// -------- 创建网络对象 --------
WiFiClient espClient;             // 普通 WiFi 连接（用于 MQTT）
PubSubClient client(espClient);   // MQTT 客户端，绑定到上面的 WiFi 连接
WiFiClientSecure secureClient;    // 加密的 WiFi 连接（用于 HTTPS 天气 API）

// -------- 天气 API 地址 --------
// 这是 Open-Meteo 免费天气 API 的 URL。
// latitude/longitude 是北京的经纬度。
// current_weather=true 表示只要当前天气（不要预报）。
const char* WEATHER_API_URL =
  "https://api.open-meteo.com/v1/forecast"
  "?latitude=39.9042&longitude=116.4074"
  "&current_weather=true&timezone=Asia/Shanghai";

// ================================================================
// 第四部分：全局状态变量
// ================================================================
//
// 这些变量在整个程序中都会被读写，
// 所以定义在所有函数外面（全局作用域）。

int currentPage   = 1;     // 当前 OLED 显示第几页（1=传感器，2=天气）
int d5_brightness = 0;     // D5 LED 当前亮度百分比

float temp = 0.0;          // 温度值（摄氏度）
float hum  = 0.0;          // 湿度值（百分比）
int light_level = 0;       // 光照等级（0~100）
bool sensor_error = false; // 传感器是否读取失败

// 天气相关
String weather_city      = "北京";   // 城市名
String weather_text      = "--";     // 天气描述（如"晴朗"、"多云"）
String weather_temp_str  = "--";     // 天气温度字符串
bool   weather_available = false;    // 是否已经成功获取过天气
bool   weather_fetch_failed = false; // 天气获取是否失败

unsigned long lastWeatherFetch = 0;  // 上次获取天气的时间戳（毫秒）
// 10 分钟 = 10 * 60 * 1000 = 600000 毫秒
const unsigned long WEATHER_INTERVAL = 600000;

// -------- 中断标志 --------
// "中断"是硬件级别的事件通知，会在任何时候突然触发，
// 所以必须用 volatile 关键字告诉编译器"这个变量可能随时被改"。
// 不加 volatile，编译器可能会"优化"掉对它的检查。
volatile bool flag_button_refresh = false;  // 按钮被按下的标志
volatile bool flag_timer_read     = false;  // 定时器到时间的标志
hw_timer_t* timer = NULL;                   // 硬件定时器对象

// -------- MQTT 重连控制 --------
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000; // 每 5 秒尝试重连一次

// -------- D6 闪烁控制 --------
// D6 的闪烁是"非阻塞"的，意思是闪烁期间程序还能干别的事。
// 具体原理：每次 loop() 执行时检查"是不是该翻转了"。
bool d6_blinking        = false;   // 是否正在闪烁
int  d6_blink_remaining = 0;      // 还剩多少次翻转（亮+灭算2次）
bool d6_blink_state     = false;  // 当前 LED 状态
unsigned long lastD6BlinkTime = 0; // 上次翻转的时间

// -------- 按键消抖 --------
// 物理按键按下时，触点会"弹跳"，导致一次按压被识别成好几次。
// 解决方法：两次按压之间必须间隔至少 200ms 才算有效。
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_MS = 200; // 消抖时间：200 毫秒

// -------- 连接状态记忆 --------
// 用来检测"刚刚连上"和"刚刚断开"的状态变化。
bool wifi_was_connected = false;
bool mqtt_was_connected = false;

// ================================================================
// 第五部分：中断服务函数（ISR）
// ================================================================
//
// "中断"是什么？想象你在看电视，突然门铃响了，
// 你暂停电视去开门，开完回来继续看 —— 这就是中断。
//
// ISR（Interrupt Service Routine）就是"开门"这个动作。
// ISR 有个铁律：要快！不能用 delay()，不能用 Serial，
// 最好只做一件事——设置一个标志位，让主循环去处理。
//
// IRAM_ATTR 的意思是：把这段代码放到 ESP32 的高速 RAM 里，
// 这样中断触发时能更快执行。

// 按键中断：当 SW1 引脚从高变低（FALLING 边沿）时触发
void IRAM_ATTR buttonISR() {
  flag_button_refresh = true;  // 只设置标志，实际处理在 loop() 里
}

// 定时器中断：每 5 秒触发一次
void IRAM_ATTR timerISR() {
  flag_timer_read = true;  // 只设置标志
}

// ================================================================
// 第六部分：工具函数
// ================================================================

// -------- 天气代码转中文 --------
// 天气 API 返回一个数字代码，我们需要把它变成中文。
// 例如：0 = 晴朗，3 = 阴天，61 = 大雨
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

// -------- D6 非阻塞闪烁 --------
//
// 什么是"非阻塞"？
//   传统写法：digitalWrite(D6, HIGH); delay(200); digitalWrite(D6, LOW); delay(200);
//   问题：delay() 期间 CPU 什么都不干，整个程序"卡住"了。
//
// 非阻塞写法：每次 loop() 执行时检查"时间到了没"，
//   如果到了就翻转 LED，没到就跳过，继续干别的事。
//   这样程序永远不会被卡住。

void startD6Blink(int times) {
  d6_blink_remaining = times * 2;  // 亮+灭 = 2次翻转，所以乘以 2
  d6_blinking = true;
  d6_blink_state = false;          // 从灭开始
  lastD6BlinkTime = millis();      // 记录当前时间
}

void updateD6Blink() {
  // 如果不在闪烁状态，直接返回，什么都不做
  if (!d6_blinking) return;

  // millis() 返回从开机到现在经过的毫秒数
  // 如果距离上次翻转已经超过 200ms，就该翻转了
  if (millis() - lastD6BlinkTime >= 200) {
    d6_blink_state = !d6_blink_state;  // 取反：true 变 false，false 变 true
    digitalWrite(D6, d6_blink_state ? HIGH : LOW); // 写入 LED
    lastD6BlinkTime = millis();         // 更新时间

    // 剩余次数减 1，减到 0 就停止闪烁
    if (--d6_blink_remaining <= 0) {
      d6_blinking = false;
      // 如果传感器没出错，把 LED 关掉
      if (!sensor_error) digitalWrite(D6, LOW);
    }
  }
}

// ================================================================
// 第七部分：传感器读取与发布
// ================================================================

void readSensors() {
  // 读取温度（摄氏度）和湿度（百分比）
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  // 读取光照传感器
  // analogRead() 返回 0~4095（ESP32 的 12 位 ADC）
  // map() 把这个范围映射到 0~100，更直观
  int raw = analogRead(GM31_PIN);
  light_level = map(raw, 0, 4095, 0, 100);

  // isnan() 检查是不是 "Not a Number"
  // DHT11 偶尔会读取失败，返回 NaN
  if (isnan(t) || isnan(h)) {
    sensor_error = true;
    digitalWrite(D6, HIGH);  // D6 常亮，提示传感器出错了
  } else {
    temp = t;
    hum  = h;
    sensor_error = false;
    digitalWrite(D6, LOW);   // D6 关掉，一切正常
  }
}

// 通过 MQTT 把传感器数据发给服务器
void publishSensorData() {
  // 如果传感器出错或 MQTT 没连上，就不发
  if (sensor_error || !client.connected()) return;

  // 用 ArduinoJson 库构建 JSON 数据
  // 格式：{"temp": 25.3, "hum": 60.1, "light": 75}
  JsonDocument doc;
  doc["temp"]  = temp;
  doc["hum"]   = hum;
  doc["light"] = light_level;

  // 把 JSON 转成字符串
  char buf[200];
  serializeJson(doc, buf);

  // 发送到 MQTT 主题 "esp32/resp/sensor"
  client.publish("esp32/resp/sensor", buf);
}

// ================================================================
// 第八部分：I2C 扫描（调试用）
// ================================================================
//
// I2C 是一种通信协议，用两根线（SDA + SCL）可以连接多个设备。
// 每个设备有一个"地址"（0x00~0x7F）。
// 这个函数遍历所有地址，看哪些设备有响应。
// 主要用于调试：确认 OLED 屏幕确实连上了。

void scanI2C() {
  Serial.println("[I2C] 扫描...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);             // 尝试和这个地址通信
    if (Wire.endTransmission() == 0) {        // 返回 0 = 有设备响应
      Serial.printf("[I2C] 设备: 0x%02X\n", addr);
    }
  }
}

// ================================================================
// 第九部分：天气 API 请求
// ================================================================
//
// 流程：
//   1. 检查 WiFi 是否连上（没网怎么请求？）
//   2. 用 HTTPS 连接天气 API
//   3. 解析返回的 JSON 数据
//   4. 提取温度和天气描述
//   5. 如果 MQTT 连着，顺便把天气发出去

bool fetchWeatherFromAPI() {
  // 第一步：检查网络
  if (WiFi.status() != WL_CONNECTED) {
    weather_fetch_failed = true;
    if (currentPage == 2) drawPage2();
    return false;
  }

  // 第二步：发起 HTTPS 请求
  HTTPClient http;
  secureClient.setInsecure();  // 跳过 SSL 证书验证（简化处理，生产环境不建议）
  http.begin(secureClient, WEATHER_API_URL);  // 指定 URL
  http.setTimeout(10000);                       // 超时 10 秒
  int httpCode = http.GET();                    // 发送 GET 请求

  // 检查 HTTP 状态码（200 = 成功）
  if (httpCode != 200) {
    http.end();
    weather_fetch_failed = true;
    startD6Blink(3);  // D6 闪 3 下表示出错
    if (currentPage == 2) drawPage2();
    return false;
  }

  // 第三步：读取响应内容
  String payload = http.getString();  // 获取 JSON 字符串
  http.end();                          // 关闭连接（重要！不关会内存泄漏）

  // 第四步：解析 JSON
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    weather_fetch_failed = true;
    startD6Blink(3);
    if (currentPage == 2) drawPage2();
    return false;
  }

  // 第五步：提取数据
  JsonObject cw = doc["current_weather"];
  if (cw.isNull()) {
    weather_fetch_failed = true;
    startD6Blink(3);
    if (currentPage == 2) drawPage2();
    return false;
  }

  // 从 JSON 中提取温度和天气代码
  float api_temp = cw["temperature"] | 0.0f;  // | 0.0f 是默认值（如果字段不存在）
  int   api_code = cw["weathercode"] | -1;

  // 保存到全局变量
  weather_city         = "北京";
  weather_text         = weathercodeToString(api_code);
  weather_temp_str     = String(api_temp, 1);  // 保留 1 位小数
  weather_available    = true;
  weather_fetch_failed = false;

  // 第六步：通过 MQTT 发送天气数据
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

// ================================================================
// 第十部分：OLED 界面绘制
// ================================================================
//
// U8G2 库的绘图流程：
//   1. clearBuffer()         — 清空"画布"（内存中的缓冲区）
//   2. drawXxx() / print()   — 在画布上画东西
//   3. sendBuffer()          — 把画布一次性推送到屏幕
//
// 这叫"双缓冲"：你不会看到画到一半的画面，
// 因为只有 sendBuffer() 之后屏幕才会更新。

// -------- 第一页：传感器数据 --------
void drawPage1() {
  u8g2.clearBuffer();  // 清空画布

  // 设置字体（支持中文的 12 号字体）
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 第一行：D5 亮度 + 页码
  u8g2.setCursor(0, 12);    // 光标移到 (0, 12) 像素位置
  u8g2.print("D5=");
  u8g2.print(d5_brightness);
  u8g2.print("%");
  u8g2.setCursor(80, 12);
  u8g2.print("[1/2]");       // 表示这是 2 页中的第 1 页

  // 画一条分割线
  u8g2.drawLine(0, 14, 128, 14);  // 从 (0,14) 到 (128,14) 画横线

  // 根据传感器状态显示数据或 "--"
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

  u8g2.sendBuffer();  // 推送到屏幕显示
}

// -------- 第二页：天气信息 --------
void drawPage2() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  u8g2.setCursor(0, 12);
  u8g2.print("[2/2] 天气信息");
  u8g2.drawLine(0, 14, 128, 14);

  // 根据状态显示不同内容
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

// 快捷函数：刷新当前页面
void refreshCurrentPage() {
  if (currentPage == 1) drawPage1();
  else                  drawPage2();
}

// ================================================================
// 第十一部分：开机动画 —— 雷达扫描效果
// ================================================================
//
// 开机时在 OLED 上播放 3.5 秒的动画：
//   左半边：逐字打出 "ENV STATION"，下面依次显示 BOOT/SENS/WIFI/MQTT/RDY
//   右半边：雷达扫描盘（同心圆 + 旋转扫描线 + 信号点）
//   底部：  ASCII 进度条 [####............] 45%

// 雷达上的信号点坐标（随机生成一次，之后固定）
static int radarDots[8][2];       // 8 个点，每个点有 x 和 y
static bool radarDotsReady = false; // 是否已经生成过

// 画雷达盘的函数
// cx, cy = 雷达中心坐标
// angle  = 当前扫描角度（弧度制，0 ~ 2π）
void drawRadar(int cx, int cy, float angle) {

  // --- 画 3 个同心圆（雷达的"环"）---
  u8g2.drawCircle(cx, cy, 8);   // 小圈
  u8g2.drawCircle(cx, cy, 17);  // 中圈
  u8g2.drawCircle(cx, cy, 25);  // 大圈

  // --- 画十字线 ---
  u8g2.drawLine(cx - 26, cy, cx + 26, cy);  // 横线
  u8g2.drawLine(cx, cy - 26, cx, cy + 26);  // 竖线

  // --- 画扫描线（亮的那条）---
  // 用三角函数算出扫描线终点坐标
  // cos(angle) 算 x 方向分量，sin(angle) 算 y 方向分量
  // 注意：屏幕的 y 轴向下，所以 sy 用减号
  int sx = cx + (int)(cos(angle) * 25);
  int sy = cy - (int)(sin(angle) * 25);
  u8g2.drawLine(cx, cy, sx, sy);

  // --- 画拖尾（扫描线后面的渐暗效果）---
  // 在主扫描线后面画几条偏移的短线，模拟"拖尾"
  for (int t = 1; t <= 4; t++) {
    float a = angle - t * 0.08;  // 角度偏移，越远偏移越大
    int tx = cx + (int)(cos(a) * 25);
    int ty = cy - (int)(sin(a) * 25);
    // 用取模运算让某些帧跳过，实现"闪烁渐暗"效果
    if (t == 1 || (t <= 3 && (int)(angle * 50) % (t + 1) == 0)) {
      u8g2.drawLine(cx, cy, tx, ty);
    }
  }

  // --- 生成随机信号点（只在第一次调用时生成）---
  if (!radarDotsReady) {
    radarDotsReady = true;
    for (int i = 0; i < 8; i++) {
      // 在中心 ±22 像素范围内随机生成
      radarDots[i][0] = cx + random(-22, 22);
      radarDots[i][1] = cy + random(-22, 22);
      // 确保点在最大圆内（用勾股定理检查距离）
      int dx = radarDots[i][0] - cx;
      int dy = radarDots[i][1] - cy;
      if (dx * dx + dy * dy > 25 * 25) {
        // 如果太远，缩小到圈内
        radarDots[i][0] = cx + dx * 7 / 10;
        radarDots[i][1] = cy + dy * 7 / 10;
      }
    }
  }

  // --- 绘制信号点 ---
  // 只有被扫描线"扫过"的点才亮起来，扫过之后逐渐变暗
  for (int i = 0; i < 8; i++) {
    // 计算这个点相对于中心的角度
    float dotAngle = atan2(cy - radarDots[i][1], radarDots[i][0] - cx);
    if (dotAngle < 0) dotAngle += 2 * PI;  // atan2 返回 -π~π，转成 0~2π

    // 计算扫描线角度和点角度的差值
    float diff = angle - dotAngle;
    if (diff < 0) diff += 2 * PI;

    // 只有差值在一定范围内的点才亮
    if (diff < PI * 0.6) {
      u8g2.drawPixel(radarDots[i][0], radarDots[i][1]);
      // 刚扫过的点画 2 像素宽（更亮）
      if (diff < PI * 0.15) {
        u8g2.drawPixel(radarDots[i][0] + 1, radarDots[i][1]);
      }
    }
  }
}

// -------- 开机动画主函数 --------
void playBootAnimation() {
  unsigned long startTime = millis();

  const int CX = 90;  // 雷达中心 x 坐标（放在屏幕右侧）
  const int CY = 32;  // 雷达中心 y 坐标

  const char* title = "ENV STATION";  // 标题文字
  int titleLen = 11;                   // 标题长度（每个字符 6 像素宽）
  int shownChars = 0;                  // 已显示的字符数

  // 加载阶段的文字列表
  // 每 700ms 显示一个阶段，模拟系统启动过程
  const char* loadText[] = {"BOOT", "SENS", "WIFI", "MQTT", "RDY!"};

  // 总时长 3500ms（3.5 秒）
  while (millis() - startTime < 3500) {
    unsigned long elapsed = millis() - startTime;

    // 计算雷达扫描角度
    // 每 2000ms 转一圈（2π 弧度）
    float angle = ((float)(elapsed % 2000) / 2000.0) * 2 * PI;

    u8g2.clearBuffer();

    // ---- 左侧：打字机效果标题 ----
    // 每 100ms 显示一个新字符
    shownChars = min((int)(elapsed / 100), titleLen);
    u8g2.setFont(u8g2_font_6x10_tf);  // 等宽英文字体
    for (int i = 0; i < shownChars; i++) {
      u8g2.setCursor(2 + i * 6, 10);  // 每个字符宽 6 像素
      u8g2.print(title[i]);
    }
    // 闪烁光标（未打完时显示）
    if (shownChars < titleLen && (elapsed / 200) % 2 == 0) {
      u8g2.setCursor(2 + shownChars * 6, 10);
      u8g2.print("_");
    }

    // ---- 左侧：加载阶段列表 ----
    // 每 700ms 进入下一阶段
    int stage = min((int)(elapsed / 700), 4);
    for (int s = 0; s <= stage && s < 5; s++) {
      u8g2.setCursor(4, 24 + s * 10);  // 每行间隔 10 像素
      if (s < stage) {
        // 已完成的阶段：显示 ">BOOT OK"
        u8g2.print(">");
        u8g2.print(loadText[s]);
        u8g2.print(" OK");
      } else if (s == stage) {
        // 当前阶段：闪烁效果（每 150ms 交替显示/隐藏）
        if ((elapsed / 150) % 2 == 0) {
          u8g2.print(">");
          u8g2.print(loadText[s]);
        }
      }
    }

    // ---- 右侧：雷达扫描 ----
    u8g2.drawLine(56, 0, 56, 64);  // 画一条竖线分隔左右
    drawRadar(CX, CY, angle);

    // ---- 底部：ASCII 进度条 ----
    int progress = map(elapsed, 0, 3500, 0, 100);  // 算百分比
    u8g2.setFont(u8g2_font_5x7_tf);  // 小号字体
    u8g2.setCursor(0, 64);
    u8g2.print("[");
    int blocks = progress / 5;  // 每 5% 一格，共 20 格
    for (int i = 0; i < 20; i++) {
      u8g2.print(i < blocks ? "#" : ".");  // 已完成用 #，未完成用 .
    }
    u8g2.print("]");
    u8g2.setCursor(92, 64);
    u8g2.print(progress);
    u8g2.print("%");

    u8g2.sendBuffer();  // 推送到屏幕
  }
}

// ================================================================
// 第十二部分：按钮处理
// ================================================================
//
// 当用户按下 SW1 按钮时：
//   1. 在屏幕上显示"刷新中"
//   2. 重新读取传感器
//   3. 重新获取天气
//   4. 刷新当前页面
//   5. 通过 MQTT 发一条"手动刷新成功"的消息

void handleButtonRefresh() {
  // 显示刷新提示（复古终端风格）
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawFrame(0, 0, 128, 64);    // 外边框
  u8g2.drawHLine(0, 10, 128);       // 标题栏下划线
  u8g2.setCursor(28, 8);
  u8g2.print("[ REFRESH ]");
  u8g2.setCursor(24, 34);
  u8g2.print("Updating...");
  u8g2.sendBuffer();

  // 执行刷新
  readSensors();
  fetchWeatherFromAPI();
  refreshCurrentPage();

  // 通过 MQTT 通知服务器
  if (client.connected()) {
    client.publish("esp32/req/manual", "手动刷新成功");
  }
}

// ================================================================
// 第十三部分：MQTT 回调函数
// ================================================================
//
// "回调"是什么意思？
//   你告诉 MQTT 库："如果有消息来了，帮我调用这个函数"。
//   MQTT 库会在收到消息时自动调用它，不需要你主动去查。
//
// MQTT 用"主题"（topic）来分类消息，类似邮箱地址。
// 我们订阅了 3 个主题：
//   esp32/req/sensor  — 请求传感器数据
//   esp32/req/weather — 请求天气数据
//   esp32/ctrl/led    — 控制 LED

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // 把主题和消息内容转成 String（更方便处理）
  String topicStr = String(topic);
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  // 根据不同主题，执行不同操作

  if (topicStr == "esp32/req/sensor") {
    // 收到传感器请求 → 读取 + 显示 + 发送
    readSensors();
    currentPage = 1;
    drawPage1();
    publishSensorData();
  }
  else if (topicStr == "esp32/req/weather") {
    // 收到天气请求 → 可以指定城市（目前只支持北京）
    String requestedCity = msg;
    if (requestedCity.length() == 0) requestedCity = "北京";
    currentPage = 2;
    drawPage2();
    fetchWeatherFromAPI();
  }
  else if (topicStr == "esp32/ctrl/led") {
    // 收到 LED 控制命令
    // ledcWrite(通道, 值)：值的范围是 0~255（因为定时器设了 8 位分辨率）
    if (msg == "LED_ON")             { ledcWrite(2, 255); d5_brightness = 100; }
    else if (msg == "LED_BRIGHT_50") { ledcWrite(2, 127); d5_brightness = 50;  }
    else if (msg == "LED_OFF")       { ledcWrite(2, 0);   d5_brightness = 0;   }
    refreshCurrentPage();
  }
}

// ================================================================
// 第十四部分：setup() —— 开机时执行一次
// ================================================================
//
// Arduino/ESP32 程序有两个必写的函数：
//   setup()  — 开机时执行一次（初始化）
//   loop()   — setup() 结束后无限循环执行
//
// setup() 里做的所有事情：
//   1. 初始化串口（调试输出）
//   2. 初始化 LED 引脚
//   3. 初始化按键引脚
//   4. 初始化 D5 的 PWM
//   5. 设置中断（按键 + 定时器）
//   6. 初始化 DHT11 传感器
//   7. 初始化 OLED 屏幕
//   8. 播放开机动画
//   9. 连接 WiFi（带进度条）
//  10. 连接 MQTT（带进度条）
//  11. 获取天气
//  12. 进入主页面

void setup() {
  // -------- 1. 初始化串口 --------
  // 波特率 115200（PC 端的串口监视器也要设成一样的）
  Serial.begin(115200);
  Serial.println("\n========== ESP32 启动 ==========");

  // -------- 2. 初始化 LED 引脚 --------
  // OUTPUT = 输出模式（ESP32 往外输出高/低电平来控制 LED）
  pinMode(D3, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D6, OUTPUT);

  // -------- 3. 初始化按键 --------
  // INPUT_PULLUP = 输入模式 + 内部上拉电阻
  // 上拉电阻的意思：默认引脚是高电平，按下按钮接地变成低电平
  pinMode(SW1, INPUT_PULLUP);

  // -------- 4. 初始化 D5 的 PWM --------
  // LEDC（LED Control）是 ESP32 的 PWM 控制器
  // ledcSetup(通道, 频率, 分辨率位数)
  //   通道 2，频率 5000Hz，8 位分辨率（0~255）
  ledcSetup(2, 5000, 8);
  ledcAttachPin(D5, 2);   // 把 D5 引脚绑定到 PWM 通道 2
  ledcWrite(2, 0);         // 初始亮度为 0（关灯）

  // -------- 5. 设置中断 --------
  // 按键中断：当 SW1 引脚出现下降沿（高→低）时，调用 buttonISR()
  attachInterrupt(digitalPinToInterrupt(SW1), buttonISR, FALLING);

  // 硬件定时器：
  //   timerBegin(定时器编号, 预分频, 是否向上计数)
  //   预分频 = 80：ESP32 主频 80MHz / 80 = 1MHz，即每 1 微秒计数 +1
  //   timerAttachInterrupt：绑定中断函数
  //   timerAlarmWrite(计数值, 是否自动重载)：5000000 微秒 = 5 秒
  timer = timerBegin(2, 80, true);
  timerAttachInterrupt(timer, &timerISR, true);
  timerAlarmWrite(timer, 5000000, true);
  timerAlarmEnable(timer);

  // -------- 6. 初始化 DHT11 --------
  dht.begin();

  // -------- 7. 初始化 OLED 屏幕 --------
  // 先启动 I2C 总线，再扫描设备（确认屏幕连上了）
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);   // I2C 时钟频率 100kHz（标准速度）
  scanI2C();               // 扫描 I2C 设备

  // 初始化 U8G2 屏幕驱动
  u8g2.setBusClock(100000);
  u8g2.begin();             // 初始化屏幕硬件
  u8g2.enableUTF8Print();   // 启用 UTF-8 打印（支持中文显示）

  // -------- 8. 播放开机动画 --------
  // 3.5 秒雷达扫描动画
  playBootAnimation();

  // -------- 9. 连接 WiFi --------
  WiFi.mode(WIFI_STA);              // STA 模式（ESP32 作为客户端去连别人的 WiFi）
  WiFi.setAutoReconnect(true);      // 断线后自动重连
  WiFi.begin(ssid, password);       // 开始连接

  unsigned long wifiStart    = millis();  // 记录开始时间（用于超时检测）
  unsigned long lastD3Toggle = 0;         // D3 上次翻转时间
  unsigned long lastOledUp   = 0;         // OLED 上次更新时间
  int dotAnim = 0;                        // 旋转指示器帧号

  // 旋转指示器字符 "|/-\"，依次显示就像在转圈
  const char* spinner = "|/-\\";

  // 循环等待，直到 WiFi 连上或超时（30 秒）
  while (WiFi.status() != WL_CONNECTED) {

    // D3 每 250ms 翻转一次（闪烁表示"正在连接"）
    if (millis() - lastD3Toggle >= 250) {
      digitalWrite(D3, !digitalRead(D3));  // 读当前状态，取反写回去
      lastD3Toggle = millis();
    }

    // OLED 每 500ms 更新一次（不要更新太快，否则闪屏）
    if (millis() - lastOledUp >= 500) {
      lastOledUp = millis();
      dotAnim = (dotAnim + 1) % 4;  // 0,1,2,3 循环

      int elapsed = (millis() - wifiStart) / 1000;  // 已过秒数
      int pct = constrain((int)(millis() - wifiStart) * 100 / 30000, 0, 100); // 进度百分比
      int blocks = pct / 5;  // 进度条格数（每 5% 一格）

      // 画 WiFi 连接界面（复古终端风格）
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tf);  // 等宽小字体

      // 外边框 + 标题栏
      u8g2.drawFrame(0, 0, 128, 64);     // 画矩形边框
      u8g2.drawHLine(0, 10, 128);        // 标题栏下划线
      u8g2.setCursor(36, 8);
      u8g2.print("[ WIFI ]");

      // 旋转指示器 + 状态文字
      u8g2.setCursor(4, 21);
      u8g2.print(spinner[dotAnim]);        // | / - \ 循环
      u8g2.print(" Connecting...");

      // SSID
      u8g2.setCursor(4, 31);
      u8g2.print("SSID: ");
      u8g2.print(ssid);

      // ASCII 进度条：[####..........] 形式
      u8g2.setCursor(4, 43);
      u8g2.print("[");
      for (int i = 0; i < 20; i++) {
        u8g2.print(i < blocks ? "#" : ".");
      }
      u8g2.print("]");

      // 底部：耗时 + 百分比
      u8g2.setCursor(4, 53);
      u8g2.print(elapsed);
      u8g2.print("s / 30s");
      u8g2.setCursor(80, 53);
      u8g2.print(pct);
      u8g2.print("%");

      u8g2.sendBuffer();
    }

    // 超时检测：超过 30 秒就放弃
    if (millis() - wifiStart > 30000) {
      Serial.println("[WiFi] 超时");
      break;
    }

    delay(10);  // 短暂延时，避免空转浪费 CPU
  }

  // -------- WiFi 连接成功的处理 --------
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(D3, HIGH);  // D3 常亮 = WiFi 已连接
    wifi_was_connected = true;

    // 显示连接成功信息（复古终端风格）
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawHLine(0, 10, 128);
    u8g2.setCursor(30, 8);
    u8g2.print("[ CONNECTED ]");

    u8g2.setCursor(4, 22);
    u8g2.print("WiFi  OK");

    // 信号强度柱状图（右上角）
    // RSSI 是信号强度，单位 dBm，越接近 0 信号越好
    //   -50 以上：极好（4 格）
    //   -60 以上：好（3 格）
    //   -70 以上：一般（2 格）
    //   -70 以下：差（1 格）
    int rssi = WiFi.RSSI();
    int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : 1;
    for (int i = 0; i < 4; i++) {
      int bh = 2 + i * 2;  // 每格高度递增：2, 4, 6, 8 像素
      if (i < bars)
        u8g2.drawBox(108 + i * 5, 15 - bh, 3, bh);    // 实心 = 有信号
      else
        u8g2.drawFrame(108 + i * 5, 15 - bh, 3, bh);  // 空心 = 无信号
    }

    // IP 地址、信号强度、频道
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
    delay(1500);  // 停留 1.5 秒让用户看到
  }

  // -------- 10. 连接 MQTT --------
  client.setServer(mqtt_server, mqtt_port);   // 设置服务器地址
  client.setCallback(mqttCallback);           // 设置收到消息时的回调函数

  unsigned long lastD4Toggle = 0;
  unsigned long lastMqttOled = 0;
  int mqttRetry = 0;      // 重试次数
  int mqttDotAnim = 0;    // 旋转指示器帧号

  // 最多重试 10 次
  while (!client.connected() && mqttRetry < 10) {

    // D4 闪烁（比 WiFi 快一点，150ms）
    if (millis() - lastD4Toggle >= 150) {
      digitalWrite(D4, !digitalRead(D4));
      lastD4Toggle = millis();
    }

    // OLED 更新
    if (millis() - lastMqttOled >= 500) {
      lastMqttOled = millis();
      mqttDotAnim = (mqttDotAnim + 1) % 4;
      int pct = mqttRetry * 100 / 10;  // 进度百分比
      int blocks = pct / 5;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x7_tf);

      // 和 WiFi 页面一样的边框风格
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

      // 进度条
      u8g2.setCursor(4, 43);
      u8g2.print("[");
      for (int i = 0; i < 20; i++) {
        u8g2.print(i < blocks ? "#" : ".");
      }
      u8g2.print("]");

      // 重试次数 + 百分比
      u8g2.setCursor(4, 53);
      u8g2.print("Retry ");
      u8g2.print(mqttRetry);
      u8g2.print("/10");
      u8g2.setCursor(80, 53);
      u8g2.print(pct);
      u8g2.print("%");

      u8g2.sendBuffer();
    }

    // 尝试连接
    // 每次用不同的客户端 ID（随机生成），避免冲突
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      // 连接成功！
      digitalWrite(D4, HIGH);  // D4 常亮
      mqtt_was_connected = true;

      // 订阅 3 个主题（告诉服务器"这几个消息我要收"）
      client.subscribe("esp32/req/sensor");
      client.subscribe("esp32/req/weather");
      client.subscribe("esp32/ctrl/led");
      Serial.println("[MQTT] 已连接");
    } else {
      // 连接失败，等 1 秒后重试
      mqttRetry++;
      delay(1000);
    }
  }

  // -------- MQTT 连接成功的处理 --------
  if (client.connected()) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawHLine(0, 10, 128);
    u8g2.setCursor(30, 8);
    u8g2.print("[ CONNECTED ]");

    u8g2.setCursor(4, 24);
    u8g2.print("MQTT  OK");

    // 列出已订阅的主题
    u8g2.setCursor(4, 38);
    u8g2.print("> sensor  topic");
    u8g2.setCursor(4, 48);
    u8g2.print("> weather topic");
    u8g2.setCursor(4, 58);
    u8g2.print("> led     topic");

    u8g2.sendBuffer();
    delay(1000);
  }

  // -------- 11. 首次获取天气 --------
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.drawHLine(0, 10, 128);
  u8g2.setCursor(28, 8);
  u8g2.print("[ WEATHER ]");
  u8g2.setCursor(30, 34);
  u8g2.print("Fetching...");
  u8g2.sendBuffer();

  fetchWeatherFromAPI();          // 获取天气
  lastWeatherFetch = millis();    // 记录时间（用于 10 分钟定时）

  // -------- 12. 进入主页面 --------
  currentPage = 1;
  readSensors();
  drawPage1();
  Serial.println("========== 初始化完成 ==========\n");
}

// ================================================================
// 第十五部分：loop() —— 主循环（无限循环执行）
// ================================================================
//
// setup() 执行完后，loop() 就开始无限循环。
// 每次循环执行以下任务：
//   1. 检查 WiFi 连接状态，控制 D3
//   2. 检查 MQTT 连接状态，控制 D4
//   3. 检查按钮是否被按下
//   4. 检查 5 秒定时器是否到了
//   5. 检查 10 分钟天气定时是否到了
//   6. 更新 D6 闪烁
//   7. yield() 让出 CPU 给系统任务

unsigned long lastWifiCheck = 0;
unsigned long lastMqttCheck = 0;

void loop() {

  // -------- 1. WiFi 状态检查 --------
  bool wifiOK = (WiFi.status() == WL_CONNECTED);

  if (wifiOK) {
    // WiFi 正常
    if (!wifi_was_connected) {
      // 刚刚从断开变为连接
      digitalWrite(D3, HIGH);
      wifi_was_connected = true;
      Serial.println("[WiFi] 重连成功");
    }
    // 已连接时什么都不做（D3 保持常亮）
  } else {
    // WiFi 断开了
    if (wifi_was_connected) {
      // 刚刚从连接变为断开
      digitalWrite(D3, LOW);
      wifi_was_connected = false;
      Serial.println("[WiFi] 断开");
    }
    // 断开状态下 D3 闪烁（每 250ms 翻转一次）
    if (millis() - lastWifiCheck > 250) {
      digitalWrite(D3, !digitalRead(D3));
      lastWifiCheck = millis();
    }
  }

  // -------- 2. MQTT 状态检查 --------
  // MQTT 必须在 WiFi 连上的前提下才能工作
  bool mqttOK = client.connected() && wifiOK;

  if (mqttOK) {
    // MQTT 正常
    if (!mqtt_was_connected) {
      // 刚刚重连成功，重新订阅主题
      digitalWrite(D4, HIGH);
      mqtt_was_connected = true;
      client.subscribe("esp32/req/sensor");
      client.subscribe("esp32/req/weather");
      client.subscribe("esp32/ctrl/led");
      Serial.println("[MQTT] 重连成功");
    }
    client.loop();  // 【重要】让 MQTT 库处理消息收发
  } else {
    // MQTT 断开
    if (mqtt_was_connected) {
      digitalWrite(D4, LOW);
      mqtt_was_connected = false;
      Serial.println("[MQTT] 断开");
    }

    if (wifiOK) {
      // WiFi 有但 MQTT 没有 → 闪烁 D4 + 尝试重连
      if (millis() - lastMqttCheck > 150) {
        digitalWrite(D4, !digitalRead(D4));
        lastMqttCheck = millis();
      }
      // 每 5 秒尝试重连一次
      if (millis() - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
        lastMqttReconnectAttempt = millis();
        String clientId = "ESP32Client-" + String(random(0xffff), HEX);
        client.connect(clientId.c_str(), mqtt_user, mqtt_pass);
      }
    } else {
      // WiFi 都没有 → D4 熄灭，不尝试 MQTT
      digitalWrite(D4, LOW);
    }
  }

  // -------- 3. 按钮处理 --------
  // 检查中断标志是否被置位
  if (flag_button_refresh) {
    flag_button_refresh = false;  // 清除标志

    // 消抖处理：两次按压必须间隔 200ms
    if (millis() - lastButtonPress > DEBOUNCE_MS) {
      lastButtonPress = millis();
      handleButtonRefresh();  // 执行刷新
    }
  }

  // -------- 4. 5 秒定时传感器读取 --------
  if (flag_timer_read) {
    flag_timer_read = false;  // 清除标志
    readSensors();             // 读取温湿度和光照
    if (currentPage == 1) drawPage1();  // 如果在第一页就刷新显示
    publishSensorData();     // 通过 MQTT 发送
  }

  // -------- 5. 10 分钟定时天气更新 --------
  if (millis() - lastWeatherFetch > WEATHER_INTERVAL) {
    lastWeatherFetch = millis();
    fetchWeatherFromAPI();
  }

  // -------- 6. D6 闪烁更新 --------
  updateD6Blink();

  // -------- 7. yield() --------
  // 让出 CPU 给 ESP32 的后台任务（WiFi 协议栈等）
  // 不调用的话，WiFi 可能会断连
  yield();
}
