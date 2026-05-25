#include "sensors.h"
#include "globals.h"
#include <ArduinoJson.h>

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int raw = analogRead(PIN_GM31);
  light_level = map(raw, 0, 4095, 0, 100);

  if (isnan(t) || isnan(h)) {
    sensor_error = true;
    digitalWrite(PIN_D6, HIGH);
  } else {
    temp = t; hum = h;
    sensor_error = false;
    digitalWrite(PIN_D6, LOW);
  }

  // 光照低于25时打开 D5 D6
  if (light_level < 25) {
    ledcWrite(PWM_CHANNEL, 255);
    d5_brightness = 100;
    digitalWrite(PIN_D6, HIGH);
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
  client.publish(TOPIC_RESP_SENSOR, buf);
}
