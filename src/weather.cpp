#include "weather.h"
#include "globals.h"
#include "display.h"
#include "d6_blink.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

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
  weather_code         = api_code;
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
    client.publish(TOPIC_RESP_WEATHER, buf);
  }

  if (currentPage == 2) drawPage2();
  return true;
}
