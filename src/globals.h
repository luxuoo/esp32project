#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <Wire.h>
#include "config.h"

// ==================== 硬件对象 ====================
DHT dht(DHTPIN, DHTTYPE);

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA
);

WiFiClient espClient;
PubSubClient client(espClient);
WiFiClientSecure secureClient;

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

volatile bool flag_button_refresh = false;
volatile bool flag_timer_read     = false;
hw_timer_t* timer = NULL;

unsigned long lastMqttReconnectAttempt = 0;

bool d6_blinking        = false;
int  d6_blink_remaining = 0;
bool d6_blink_state     = false;
unsigned long lastD6BlinkTime = 0;

unsigned long lastButtonPress = 0;

bool wifi_was_connected = false;
bool mqtt_was_connected = false;

// ==================== 中断 ====================
void IRAM_ATTR buttonISR() {
  flag_button_refresh = true;
}
void IRAM_ATTR timerISR() {
  flag_timer_read = true;
}

// ==================== 跨模块函数前向声明 ====================
void drawPage1();
void drawPage2();
void refreshCurrentPage();
void playBootAnimation();
void handleButtonRefresh();
void startD6Blink(int times);
void updateD6Blink();
void readSensors();
void publishSensorData();
bool fetchWeatherFromAPI();
void scanI2C();
