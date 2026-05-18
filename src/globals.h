#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <Wire.h>
#include "config.h"

// ==================== 硬件对象 ====================
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern DHT dht;
extern WiFiClient espClient;
extern PubSubClient client;
extern WiFiClientSecure secureClient;

// ==================== 页面与传感器 ====================
extern int   currentPage;
extern int   d5_brightness;
extern float temp, hum;
extern int   light_level;
extern bool  sensor_error;

// ==================== 天气 ====================
extern String weather_city;
extern String weather_text;
extern String weather_temp_str;
extern bool   weather_available;
extern bool   weather_fetch_failed;

// ==================== 定时器与标志 ====================
extern unsigned long lastWeatherFetch;
extern volatile bool flag_button_refresh;
extern volatile bool flag_timer_read;
extern hw_timer_t*   timer;

// ==================== MQTT 重连 ====================
extern unsigned long lastMqttReconnectAttempt;

// ==================== D6 闪烁 ====================
extern bool          d6_blinking;
extern int           d6_blink_remaining;
extern bool          d6_blink_state;
extern unsigned long lastD6BlinkTime;

// ==================== 按钮 ====================
extern unsigned long lastButtonPress;

// ==================== 连接状态 ====================
extern bool wifi_was_connected;
extern bool mqtt_was_connected;
