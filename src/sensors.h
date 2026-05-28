#pragma once

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
    digitalWrite(PIN_D6, d6_blink_state ? HIGH : LOW);
    lastD6BlinkTime = millis();
    if (--d6_blink_remaining <= 0) {
      d6_blinking = false;
      if (!sensor_error) digitalWrite(PIN_D6, LOW);
    }
  }
}

// ==================== 传感器读取 ====================
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
}

// ==================== 传感器数据发布 ====================
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
