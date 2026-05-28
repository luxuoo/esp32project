#pragma once

// ==================== 传感器数据页 ====================
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
    u8g2.drawFrame(6, 20, 3, 12);
    u8g2.drawDisc(5, 34, 3);
    u8g2.setCursor(14, 30); u8g2.print("温度: -- C");

    u8g2.drawTriangle(6, 38, 2, 44, 10, 44);
    u8g2.drawDisc(6, 45, 3);
    u8g2.setCursor(14, 45); u8g2.print("湿度: -- %RH");

    u8g2.drawCircle(6, 57, 3);
    u8g2.drawLine(6, 52, 6, 50); u8g2.drawLine(6, 62, 6, 64);
    u8g2.drawLine(1, 57, 0, 57); u8g2.drawLine(11, 57, 13, 57);
    u8g2.setCursor(14, 60); u8g2.print("光照: -- 级");
  } else {
    u8g2.drawFrame(6, 20, 3, 12);
    u8g2.drawDisc(5, 34, 3);
    u8g2.setCursor(14, 30); u8g2.print("温度: "); u8g2.print(temp, 1); u8g2.print(" C");

    u8g2.drawTriangle(6, 38, 2, 44, 10, 44);
    u8g2.drawDisc(6, 45, 3);
    u8g2.setCursor(14, 45); u8g2.print("湿度: "); u8g2.print(hum, 1); u8g2.print(" %RH");

    u8g2.drawCircle(6, 57, 3);
    u8g2.drawLine(6, 52, 6, 50); u8g2.drawLine(6, 62, 6, 64);
    u8g2.drawLine(1, 57, 0, 57); u8g2.drawLine(11, 57, 13, 57);
    u8g2.setCursor(14, 60); u8g2.print("光照: "); u8g2.print(light_level); u8g2.print(" 级");
  }
  u8g2.sendBuffer();
}

// ==================== 天气信息页 ====================
void drawPage2() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, 12);
  u8g2.print("[2/2] 天气信息");
  u8g2.drawHLine(0, 14, 128);

  if (weather_fetch_failed) {
    u8g2.drawLine(50, 28, 78, 50); u8g2.drawLine(78, 28, 50, 50);
    u8g2.setCursor(32, 60); u8g2.print("获取失败");
  } else if (weather_available) {
    u8g2.setFont(u8g2_font_logisoso24_tf);
    u8g2.setCursor(4, 44);
    u8g2.print(weather_temp_str);

    u8g2.drawCircle(4 + weather_temp_str.length() * 14 + 2, 24, 2);

    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(4 + weather_temp_str.length() * 14 + 6, 28);
    u8g2.print("C");

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

    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setCursor(68, 50); u8g2.print(weather_city);
    u8g2.setCursor(68, 63); u8g2.print(weather_text);

    u8g2.drawHLine(0, 63, 64);
  } else {
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

// ==================== 页面切换 ====================
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

// ==================== 开机动画（星空粒子）====================
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

    // 星空：漂浮星星
    for (int i = 0; i < 30; i++) {
      int dx = (int)(sin(elapsed * 0.001 + i * 1.7) * 2);
      int dy = (int)(cos(elapsed * 0.0008 + i * 2.3) * 1);
      drawStar(starX[i] + dx, starY[i] + dy, starAge[i] + (int)(elapsed / 500));
    }

    // 波浪线（远/近两层）
    for (int x = 0; x < 128; x++) {
      int y1 = 50 + (int)(sin(x * 0.05 + elapsed * 0.003) * 3);
      int y2 = 53 + (int)(sin(x * 0.07 + elapsed * 0.002 + 1.5) * 2);
      u8g2.drawPixel(x, y1);
      if (x % 2 == 0) u8g2.drawPixel(x, y2);
    }

    // 标题：逐字淡入
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

    // 副标题
    if (elapsed > 600) {
      int subChars = min((int)((elapsed - 600) / 60), subLen);
      u8g2.setFont(u8g2_font_5x7_tf);
      for (int i = 0; i < subChars; i++) {
        u8g2.setCursor(26 + i * 5, 26);
        u8g2.print(subtitle[i]);
      }
    }

    // 加载阶段（两列布局）
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

    // 底部：进度条
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

// ==================== 按钮刷新界面 ====================
void handleButtonRefresh() {
  Serial.println("[BTN] 手动刷新所有数据");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  int rcx = 64, rcy = 24;
  u8g2.drawCircle(rcx, rcy, 10);
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
