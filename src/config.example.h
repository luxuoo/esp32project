#pragma once

// ==================== WiFi ====================
#define WIFI_SSID     "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"

// ==================== MQTT ====================
#define MQTT_SERVER "你的MQTT服务器地址"
#define MQTT_PORT   1883
#define MQTT_USER   "你的MQTT用户名"
#define MQTT_PASS   "你的MQTT密码"

// MQTT 主题
#define TOPIC_REQ_SENSOR   "esp32/req/sensor"
#define TOPIC_REQ_WEATHER  "esp32/req/weather"
#define TOPIC_CTRL_LED     "esp32/ctrl/led"
#define TOPIC_RESP_SENSOR  "esp32/resp/sensor"
#define TOPIC_RESP_WEATHER "esp32/resp/weather"
#define TOPIC_REQ_MANUAL   "esp32/req/manual"

// ==================== 引脚 ====================
#define PIN_D3  14
#define PIN_D4  27
#define PIN_D5  26
#define PIN_D6  33
#define PIN_SW1 32

#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22

#define DHTPIN  4
#define DHTTYPE DHT11

#define PIN_GM31 35

// ==================== PWM ====================
#define PWM_CHANNEL 2
#define PWM_FREQ    5000
#define PWM_RES     8

// ==================== 天气 API ====================
#define WEATHER_API_URL \
  "https://api.open-meteo.com/v1/forecast" \
  "?latitude=39.9042&longitude=116.4074" \
  "&current_weather=true&timezone=Asia/Shanghai"

// ==================== 时间间隔 ====================
#define WEATHER_INTERVAL        600000UL  // 10 分钟
#define MQTT_RECONNECT_INTERVAL 5000UL    // 5 秒
#define DEBOUNCE_MS             200UL     // 按钮消抖
