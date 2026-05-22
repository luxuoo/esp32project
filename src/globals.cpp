#include "globals.h"

// ==================== 硬件对象 ====================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA
);
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);
WiFiClientSecure secureClient;

// ==================== 页面与传感器 ====================
int   currentPage   = 1;
int   d5_brightness = 0;
float temp = 0.0, hum = 0.0;
int   light_level   = 0;
bool  sensor_error  = false;

// ==================== 天气 ====================
String weather_city         = "北京";
String weather_text         = "--";
String weather_temp_str     = "--";
int    weather_code         = -1;
bool   weather_available    = false;
bool   weather_fetch_failed = false;

// ==================== 定时器与标志 ====================
unsigned long lastWeatherFetch = 0;
volatile bool flag_button_refresh = false;
volatile bool flag_timer_read     = false;
hw_timer_t*   timer = NULL;

// ==================== MQTT 重连 ====================
unsigned long lastMqttReconnectAttempt = 0;

// ==================== D6 闪烁 ====================
bool          d6_blinking        = false;
int           d6_blink_remaining = 0;
bool          d6_blink_state     = false;
unsigned long lastD6BlinkTime    = 0;

// ==================== 按钮 ====================
unsigned long lastButtonPress = 0;

// ==================== 连接状态 ====================
bool wifi_was_connected = false;
bool mqtt_was_connected = false;
