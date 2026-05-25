#include "display.h"
#include "globals.h"
#include <math.h>

// ==================== 横向条形仪表 ====================
void drawBarGauge(int y, const char* label, const char* value,
                  float ratio, int barH) {
  // 标签 (左侧, 留边距)
  u8g2.setCursor(4, y);
  u8g2.print(label);

  // 条形 (标签右侧, 长度减半)
  int labelW = u8g2.getStrWidth(label);
  int barX   = 4 + labelW + 5;
  int barW   = (128 - barX - 4) / 2;
  if (barW < 16) barW = 16;
  int barY = y + 2;

  u8g2.drawFrame(barX, barY, barW, barH);

  if (ratio < 0) ratio = 0;
  if (ratio > 1) ratio = 1;
  int fillW = (int)(ratio * (barW - 2));
  if (fillW > 0) {
    u8g2.drawBox(barX + 1, barY + 1, fillW, barH - 2);
  }

  // 数值 (条形右侧)
  u8g2.setCursor(barX + barW + 3, y);
  u8g2.print(value);
}

// ==================== 光照强度条 (VU表分段) ====================
void drawLightBar(int x, int y, int w, int h, int value, int maxVal) {
  u8g2.setCursor(x, y - 1);
  u8g2.print("光:");

  int barX  = x + 20;
  int barW  = 70;
  int barH  = h;

  const int segs = 10;
  int segW  = (barW - (segs - 1)) / segs;
  int totalSegW = segW * segs + (segs - 1);

  u8g2.drawFrame(barX, y, totalSegW + 2, barH + 2);

  int filledSegs = map(value, 0, maxVal, 0, segs);
  if (filledSegs < 0) filledSegs = 0;
  if (filledSegs > segs) filledSegs = segs;

  for (int i = 0; i < segs; i++) {
    int sx = barX + 1 + i * (segW + 1);
    if (i < filledSegs) {
      u8g2.drawBox(sx, y + 1, segW, barH);
    } else {
      u8g2.drawHLine(sx, y + barH, segW);
    }
  }

  char buf[6];
  snprintf(buf, sizeof(buf), "%d", value);
  u8g2.setCursor(barX + totalSegW + 4, y + barH + 1);
  u8g2.print(buf);
}

// ==================== 天气像素图标 ====================
static const uint8_t WEATHER_ICONS[][32] = {
  // 0: 晴朗 ☀
  {
    0b00001001, 0b10010000,
    0b00100000, 0b00000100,
    0b00000111, 0b11100000,
    0b00001111, 0b11110000,
    0b10011111, 0b11111001,
    0b00001111, 0b11110000,
    0b00000111, 0b11100000,
    0b00100000, 0b00000100,
    0b00001001, 0b10010000,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
  },
  // 1: 多云 ☁
  {
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x3C,
    0x00, 0x7E,
    0x00, 0xFF,
    0x07, 0xFF,
    0x0F, 0xFF,
    0x1F, 0xFF,
    0x3F, 0xFF,
    0x7F, 0xFF,
    0x7F, 0xFF,
    0x3F, 0xFE,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
  },
  // 2: 雨
  {
    0x00, 0x00,
    0x00, 0x3C,
    0x00, 0x7E,
    0x01, 0xFF,
    0x03, 0xFF,
    0x07, 0xFF,
    0x0F, 0xFF,
    0x07, 0xFE,
    0x00, 0x00,
    0x44, 0x44,
    0x22, 0x22,
    0x11, 0x11,
    0x08, 0x88,
    0x04, 0x44,
    0x00, 0x00,
    0x00, 0x00,
  },
  // 3: 雪
  {
    0x00, 0x00,
    0x00, 0x3C,
    0x00, 0x7E,
    0x01, 0xFF,
    0x03, 0xFF,
    0x07, 0xFF,
    0x0F, 0xFF,
    0x07, 0xFE,
    0x00, 0x00,
    0x0A, 0x50,
    0x04, 0x20,
    0x11, 0x44,
    0x04, 0x20,
    0x0A, 0x50,
    0x00, 0x00,
    0x00, 0x00,
  },
  // 4: 雷暴
  {
    0x00, 0x00,
    0x00, 0x3C,
    0x00, 0x7E,
    0x01, 0xFF,
    0x03, 0xFF,
    0x07, 0xFF,
    0x0F, 0xFF,
    0x07, 0xFE,
    0x00, 0x00,
    0x01, 0xE0,
    0x03, 0xC0,
    0x07, 0x80,
    0x0F, 0x00,
    0x06, 0x00,
    0x00, 0x00,
    0x00, 0x00,
  },
  // 5: 雾
  {
    0x00, 0x00,
    0x00, 0x00,
    0x3F, 0xFC,
    0x00, 0x00,
    0x1F, 0xF8,
    0x00, 0x00,
    0x0F, 0xF0,
    0x00, 0x00,
    0x1F, 0xF8,
    0x00, 0x00,
    0x3F, 0xFC,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
  },
};

void drawWeatherIcon(int x, int y, int code) {
  int idx;
  if      (code == 0)               idx = 0;
  else if (code <= 3)               idx = 1;
  else if (code >= 45 && code <= 48) idx = 5;
  else if (code >= 51 && code <= 67) idx = 2;
  else if (code >= 71 && code <= 77) idx = 3;
  else if (code >= 80 && code <= 84) idx = 2;
  else if (code >= 95)              idx = 4;
  else                              idx = 1;

  for (int row = 0; row < 16; row++) {
    uint8_t hi = WEATHER_ICONS[idx][row * 2];
    uint8_t lo = WEATHER_ICONS[idx][row * 2 + 1];
    uint16_t bits = ((uint16_t)hi << 8) | lo;
    for (int col = 0; col < 16; col++) {
      if (bits & (0x8000 >> col)) {
        u8g2.drawPixel(x + col, y + row);
      }
    }
  }
}

// ==================== 像素打勾图标 ====================
void drawPixelCheck(int cx, int cy) {
  static const uint8_t check[] = {
    0b00000000, 0b01000000,
    0b00000000, 0b11000000,
    0b00000001, 0b10000000,
    0b11000011, 0b00000000,
    0b11100110, 0b00000000,
    0b01111100, 0b00000000,
    0b00111000, 0b00000000,
    0b00010000, 0b00000000,
  };
  int ox = cx - 5, oy = cy - 4;
  for (int row = 0; row < 8; row++) {
    uint8_t hi = check[row * 2];
    uint8_t lo = check[row * 2 + 1];
    uint16_t bits = ((uint16_t)hi << 8) | lo;
    for (int col = 0; col < 11; col++) {
      if (bits & (0x8000 >> col)) {
        u8g2.drawPixel(ox + col, oy + row);
      }
    }
  }
}

// ==================== 连接状态屏幕 ====================
void drawConnectingScreen(const char* title, const char* detail,
                          const char* status, int pct) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  int tw = u8g2.getStrWidth(title);
  u8g2.setCursor(64 - tw / 2, 14);
  u8g2.print(title);

  u8g2.setCursor(4, 30);
  u8g2.print(detail);

  int barW = 120;
  u8g2.drawFrame(4, 38, barW, 10);
  int fillW = pct * (barW - 2) / 100;
  if (fillW > 0) u8g2.drawBox(5, 39, fillW, 8);

  u8g2.setCursor(4, 60);
  u8g2.print(status);

  u8g2.sendBuffer();
}

void drawConnectedScreen(const char* title, const char* ip, int rssi) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  int tw = u8g2.getStrWidth(title);
  u8g2.setCursor(64 - tw / 2 - 8, 14);
  u8g2.print(title);
  drawPixelCheck(64 + tw / 2 + 6, 10);

  u8g2.setCursor(0, 34);
  u8g2.print("IP: "); u8g2.print(ip);
  u8g2.setCursor(0, 52);
  u8g2.print("信号: "); u8g2.print(rssi); u8g2.print(" dBm");

  u8g2.sendBuffer();
}

// ==================== 页面 1: 传感器仪表盘 ====================
void drawPage1() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 页面指示器
  u8g2.drawDisc(58, 3, 2);
  u8g2.drawCircle(70, 3, 2);

  if (sensor_error) {
    drawBarGauge(12, "温度",  "-- C",    0, 6);
    drawBarGauge(28, "湿度",  "-- %RH",  0, 6);
    drawBarGauge(44, "光照",  "-- 级",   0, 6);
  } else {
    char tv[12];
    snprintf(tv, sizeof(tv), "%.1f C", temp);
    drawBarGauge(12, "温度", tv, temp / 40.0f, 6);

    char hv[12];
    snprintf(hv, sizeof(hv), "%.1f %%", hum);
    drawBarGauge(28, "湿度", hv, hum / 100.0f, 6);

    char lv[12];
    snprintf(lv, sizeof(lv), "%d 级", light_level);
    drawBarGauge(44, "光照", lv, light_level / 100.0f, 6);
  }

  // D5 亮度
  char bri[8];
  snprintf(bri, sizeof(bri), "D5:%d%%", d5_brightness);
  u8g2.setCursor(96, 63);
  u8g2.print(bri);

  u8g2.sendBuffer();
}

// ==================== 页面 2: 天气 ====================
void drawPage2() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 页面指示器
  u8g2.drawCircle(58, 3, 2);
  u8g2.drawDisc(70, 3, 2);

  if (weather_fetch_failed) {
    u8g2.setCursor(0, 26);
    u8g2.print("获取天气失败");
    u8g2.setCursor(0, 44);
    u8g2.print("请检查网络连接");
  } else if (weather_available) {
    // 上半: 图标 + 城市/天气
    drawWeatherIcon(4, 10, weather_code);

    u8g2.setCursor(26, 16);
    u8g2.print(weather_city);

    u8g2.setCursor(26, 32);
    u8g2.print(weather_text);

    u8g2.drawHLine(0, 42, 128);

    // 下半: 温度大居中
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%s C", weather_temp_str.c_str());
    int tw = u8g2.getStrWidth(tbuf);
    u8g2.setCursor(64 - tw / 2, 58);
    u8g2.print(tbuf);
  } else {
    // 加载动画
    unsigned long t = millis() / 150;
    for (int i = 0; i < 8; i++) {
      float angle = (t * 45 + i * 45) * M_PI / 180.0;
      int px = 64 + (int)(12 * cos(angle));
      int py = 24 + (int)(12 * sin(angle));
      if (i <= (t % 8))
        u8g2.drawDisc(px, py, 1);
      else
        u8g2.drawPixel(px, py);
    }
    u8g2.setCursor(20, 44);
    u8g2.print("正在获取天气中");
  }

  u8g2.sendBuffer();
}

// ==================== 页面切换 ====================
void refreshCurrentPage() {
  if (currentPage == 1) drawPage1();
  else                  drawPage2();
}

// ==================== 像素扩散开机动画 ====================
void playBootAnimation() {
  unsigned long startTime = millis();
  const int cx = 64, cy = 32;
  u8g2.setFont(u8g2_font_6x12_tf);

  while (millis() - startTime < 3000) {
    unsigned long elapsed = millis() - startTime;
    u8g2.clearBuffer();

    if (elapsed < 1200) {
      // 粒子螺旋扩散
      int frame = elapsed / 25;
      int maxR = frame * 2;
      for (int i = 0; i < 40; i++) {
        float angle = i * 0.7854 + frame * 0.1;
        float radius = (i * 3 + frame * 2) % (maxR + 1);
        int px = cx + (int)(radius * cos(angle));
        int py = cy + (int)(radius * sin(angle));
        if (px >= 0 && px < 128 && py >= 0 && py < 64)
          u8g2.drawPixel(px, py);
      }
      for (int r = 0; r < maxR && r < 30; r += 5)
        u8g2.drawCircle(cx, cy, r);
      if ((frame / 4) % 2 == 0)
        u8g2.drawDisc(cx, cy, 2);

    } else if (elapsed < 2200) {
      // 文字逐字打出
      int phase = (elapsed - 1200) / 50;
      for (int r = 5; r < 26; r += 5)
        u8g2.drawCircle(cx, cy, r);

      const char* title = "wwwww";
      int len = strlen(title);
      if (phase < len) {
        u8g2.setCursor(cx - (len * 3) + phase * 6, cy - 4);
        for (int i = 0; i <= phase && i < len; i++)
          u8g2.print(title[i]);
        if ((elapsed / 200) % 2 == 0) {
          int curX = cx - (len * 3) + (phase + 1) * 6;
          u8g2.drawBox(curX, cy - 6, 5, 9);
        }
      } else {
        u8g2.setCursor(cx - (len * 3), cy - 4);
        u8g2.print(title);
      }
      for (int x = 0; x < 128; x += 4) {
        int waveY = 50 + (int)(3.0 * sin((x + elapsed * 2) * 0.15));
        u8g2.drawPixel(x, waveY);
      }

    } else {
      // 最终画面
      u8g2.setFont(u8g2_font_wqy12_t_gb2312);
      const char* title = "wwwww";
      int tw = u8g2.getStrWidth(title);
      u8g2.setCursor(64 - tw / 2, 26);
      u8g2.print(title);

      u8g2.drawHLine(10, 15, 108);
      u8g2.drawHLine(10, 40, 108);
      u8g2.drawPixel(4, 4);
      u8g2.drawPixel(123, 4);
      u8g2.drawPixel(4, 59);
      u8g2.drawPixel(123, 59);

      int barPct = map(elapsed - 2200, 0, 800, 0, 100);
      if (barPct > 100) barPct = 100;
      u8g2.drawFrame(24, 48, 80, 6);
      if (barPct > 0)
        u8g2.drawBox(25, 49, barPct * 78 / 100, 4);

      char pctBuf[6];
      snprintf(pctBuf, sizeof(pctBuf), "%d%%", barPct);
      int pw = u8g2.getStrWidth(pctBuf);
      u8g2.setCursor(64 - pw / 2, 63);
      u8g2.print(pctBuf);
    }

    u8g2.sendBuffer();
  }
}
