#include "display.h"
#include "globals.h"
#include <math.h>

// ==================== 仪表盘弧形表盘 ====================
void drawGauge(int cx, int cy, int r, float value, float minVal, float maxVal,
               const char* label, const char* unit, int decimals) {
  const int segs = 8;
  // 弧形从左下(225°) 经过正上方(270°= -90°) 到右下(315°)
  // U8g2: y轴向下, cos/sin 可直接用于指针
  float startDeg = 225.0;
  float endDeg   = 315.0;
  float step     = (endDeg - startDeg) / segs;

  // 计算填充段数
  float ratio = 0;
  if (maxVal > minVal) {
    ratio = (value - minVal) / (maxVal - minVal);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
  }
  int filledSegs = (int)(ratio * segs + 0.5f);
  if (filledSegs > segs) filledSegs = segs;

  // 绘制段落线 (暗色背景段用点线)
  for (int i = 0; i < segs; i++) {
    float angle = (startDeg + i * step) * M_PI / 180.0;
    int x1 = cx + (int)(r * cos(angle));
    int y1 = cy - (int)(r * sin(angle));
    int x2 = cx + (int)((r - 6) * cos(angle));
    int y2 = cy - (int)((r - 6) * sin(angle));

    if (i < filledSegs) {
      // 亮色填充段 —— 粗实线
      u8g2.drawLine(x1, y1, x2, y2);
      // 加粗: 偏移1px再画一条
      if (y1 >= cy) u8g2.drawLine(x1, y1 - 1, x2, y2 - 1);
      else          u8g2.drawLine(x1, y1 + 1, x2, y2 + 1);
    } else {
      // 暗色未填充段 —— 点线
      for (int d = 0; d < 7; d += 2) {
        float frac = d / 6.0f;
        u8g2.drawPixel(x1 + (int)((x2 - x1) * frac),
                       y1 + (int)((y2 - y1) * frac));
      }
    }
  }

  // 弧形两端装饰小圆点
  float aL = startDeg * M_PI / 180.0;
  float aR = endDeg * M_PI / 180.0;
  u8g2.drawDisc(cx + (int)(r * cos(aL)), cy - (int)(r * sin(aL)), 1);
  u8g2.drawDisc(cx + (int)(r * cos(aR)), cy - (int)(r * sin(aR)), 1);

  // 绘制指针
  if (maxVal > minVal) {
    float pAngle = (startDeg + ratio * (endDeg - startDeg)) * M_PI / 180.0;
    int px = cx + (int)((r - 2) * cos(pAngle));
    int py = cy - (int)((r - 2) * sin(pAngle));
    u8g2.drawLine(cx, cy, px, py);
  }
  u8g2.drawDisc(cx, cy, 2);  // 中心轴心

  // 数值
  char buf[12];
  if (decimals == 0) snprintf(buf, sizeof(buf), "%d", (int)value);
  else               snprintf(buf, sizeof(buf), "%.*f", decimals, value);
  u8g2.setCursor(cx - (int)(u8g2.getStrWidth(buf) / 2.0), cy + 11);
  u8g2.print(buf);

  // 标签
  u8g2.setCursor(cx - (int)(u8g2.getStrWidth(label) / 2.0), cy + 23);
  u8g2.print(label);
}

// ==================== 光照强度条 (VU表风格) ====================
void drawLightBar(int x, int y, int w, int h, int value, int maxVal) {
  // 左侧标签
  u8g2.setCursor(x, y - 1);
  u8g2.print("光:");

  int barX  = x + 16;
  int barW  = w - 16;
  int barH  = h - 1;

  // 分段式条形 (10格)
  const int segs = 10;
  int segW  = (barW - (segs - 1)) / segs;  // 每格宽度
  int totalSegW = segW * segs + (segs - 1); // 总占用宽度

  // 边框
  u8g2.drawFrame(barX, y, totalSegW + 2, barH + 2);

  int filledSegs = map(value, 0, maxVal, 0, segs);
  if (filledSegs < 0) filledSegs = 0;
  if (filledSegs > segs) filledSegs = segs;

  for (int i = 0; i < segs; i++) {
    int sx = barX + 1 + i * (segW + 1);
    int sy = y + 1;
    int sh = barH;
    if (i < filledSegs) {
      u8g2.drawBox(sx, sy, segW, sh);
    } else {
      // 暗格: 只画底部线
      u8g2.drawHLine(sx, sy + sh - 1, segW);
    }
  }

  // 右侧数值
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", value);
  u8g2.setCursor(barX + totalSegW + 5, y + barH);
  u8g2.print(buf);
}

// ==================== 天气像素图标 ====================
// 16x16 像素画图标, 每行2字节
static const uint8_t WEATHER_ICONS[][32] = {
  // 0: 晴朗 ☀ (太阳+光芒)
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
  // 1: 多云 ☁ (云朵)
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
  // 2: 雨 (云+雨滴)
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
  // 3: 雪 (云+雪花)
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
  // 4: 雷暴 (云+闪电)
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
  // 5: 雾 (水平线)
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
  if      (code == 0)               idx = 0; // 晴朗
  else if (code <= 3)               idx = 1; // 多云/阴
  else if (code >= 45 && code <= 48) idx = 5; // 雾
  else if (code >= 51 && code <= 67) idx = 2; // 雨
  else if (code >= 71 && code <= 77) idx = 3; // 雪
  else if (code >= 80 && code <= 84) idx = 2; // 阵雨
  else if (code >= 95)              idx = 4; // 雷暴
  else                              idx = 1; // 默认多云

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
  // 11x9 像素对勾
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

// ==================== 连接状态屏幕 (用于 main.cpp) ====================
void drawConnectingScreen(const char* title, const char* detail,
                          const char* status, int pct) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 标题居中
  int tw = u8g2.getStrWidth(title);
  u8g2.setCursor(64 - tw / 2, 14);
  u8g2.print(title);

  // 详情信息
  u8g2.setCursor(4, 30);
  u8g2.print(detail);

  // 进度条
  int barW = 120;
  int barX = 4;
  u8g2.drawFrame(barX, 38, barW, 10);
  int fillW = pct * (barW - 2) / 100;
  if (fillW > 0) u8g2.drawBox(barX + 1, 39, fillW, 8);

  // 状态文字
  u8g2.setCursor(4, 60);
  u8g2.print(status);

  u8g2.sendBuffer();
}

void drawConnectedScreen(const char* title, const char* ip, int rssi) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 标题 + 对勾
  int tw = u8g2.getStrWidth(title);
  u8g2.setCursor(64 - tw / 2 - 8, 14);
  u8g2.print(title);
  drawPixelCheck(64 + tw / 2 + 6, 10);

  // 信息
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
  u8g2.drawDisc(58, 4, 2);
  u8g2.drawCircle(70, 4, 2);

  if (sensor_error) {
    // 错误状态
    drawGauge(34, 26, 18, 0, 0, 1, "温度", "C", 1);
    drawGauge(94, 26, 18, 0, 0, 1, "湿度", "%RH", 1);
    drawLightBar(4, 44, 120, 10, 0, 100);
  } else {
    drawGauge(34, 26, 18, temp, 0, 40, "温度", "C", 1);
    drawGauge(94, 26, 18, hum, 0, 100, "湿度", "%RH", 1);
    drawLightBar(4, 44, 120, 10, light_level, 100);
  }

  // D5 亮度指示 (右上角小字)
  char bri[8];
  snprintf(bri, sizeof(bri), "D5:%d%%", d5_brightness);
  u8g2.setCursor(100, 4);
  u8g2.print(bri);

  u8g2.sendBuffer();
}

// ==================== 页面 2: 天气仪表盘 ====================
void drawPage2() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  // 页面指示器
  u8g2.drawCircle(58, 4, 2);
  u8g2.drawDisc(70, 4, 2);

  if (weather_fetch_failed) {
    // 失败状态
    u8g2.setCursor(0, 30);
    u8g2.print("获取天气失败");
    u8g2.setCursor(0, 48);
    u8g2.print("请检查网络连接");
  } else if (weather_available) {
    // 左侧: 天气图标
    drawWeatherIcon(4, 8, weather_code);

    // 右侧: 信息
    u8g2.setCursor(24, 14);
    u8g2.print(weather_city);

    // 分隔线
    u8g2.drawHLine(24, 18, 100);

    u8g2.setCursor(24, 32);
    u8g2.print(weather_text);

    // 温度 (大号居中)
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%s C", weather_temp_str.c_str());
    int tw = u8g2.getStrWidth(tbuf);
    u8g2.setCursor(64 - tw / 2, 56);
    u8g2.print(tbuf);
  } else {
    // 加载中 —— 像素加载动画
    unsigned long t = millis() / 150;
    for (int i = 0; i < 8; i++) {
      float angle = (t * 45 + i * 45) * M_PI / 180.0;
      int dist = 12;
      int px = 64 + (int)(dist * cos(angle));
      int py = 32 + (int)(dist * sin(angle));
      if (i <= (t % 8))
        u8g2.drawDisc(px, py, 1);
      else
        u8g2.drawPixel(px, py);
    }
    u8g2.setCursor(32, 56);
    u8g2.print("获取天气中...");
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
  u8g2.setFont(u8g2_font_6x10);

  while (millis() - startTime < 3000) {
    unsigned long elapsed = millis() - startTime;

    u8g2.clearBuffer();

    if (elapsed < 1200) {
      // === 第一阶段: 粒子从中心扩散 ===
      int frame = elapsed / 25;
      int maxR = frame * 2;

      // 螺旋扩散粒子
      for (int i = 0; i < 40; i++) {
        float angle = i * 0.7854 + frame * 0.1;
        float radius = (i * 3 + frame * 2) % (maxR + 1);
        int px = cx + (int)(radius * cos(angle));
        int py = cy + (int)(radius * sin(angle));
        if (px >= 0 && px < 128 && py >= 0 && py < 64) {
          u8g2.drawPixel(px, py);
        }
      }

      // 同心圆环
      for (int r = 0; r < maxR && r < 30; r += 5) {
        u8g2.drawCircle(cx, cy, r);
      }

      // 中心脉冲
      if ((frame / 4) % 2 == 0) {
        u8g2.drawDisc(cx, cy, 2);
      }

    } else if (elapsed < 2200) {
      // === 第二阶段: 文字逐行出现 ===
      int phase = (elapsed - 1200) / 50;

      // 背景: 淡化的圆环
      for (int r = 5; r < 26; r += 5) {
        u8g2.drawCircle(cx, cy, r);
      }

      // "24 LuXuXu" 逐字符出现
      const char* title = "24 LuXuXu";
      int len = strlen(title);
      if (phase < len) {
        u8g2.setCursor(cx - (len * 3) + phase * 6, cy - 4);
        for (int i = 0; i <= phase && i < len; i++) {
          u8g2.print(title[i]);
        }
        // 闪烁光标
        if ((elapsed / 200) % 2 == 0) {
          int curX = cx - (len * 3) + (phase + 1) * 6;
          u8g2.drawBox(curX, cy - 6, 5, 9);
        }
      } else {
        // 全部显示完毕
        u8g2.setCursor(cx - (len * 3), cy - 4);
        u8g2.print(title);
      }

      // 下方进度指示: 像素波浪
      for (int x = 0; x < 128; x += 4) {
        int waveY = 50 + (int)(3.0 * sin((x + elapsed * 2) * 0.15));
        u8g2.drawPixel(x, waveY);
      }

    } else {
      // === 第三阶段: 聚合 —— 标题居中, 装饰边框 ===
      u8g2.setFont(u8g2_font_wqy12_t_gb2312);

      // 中心标题
      const char* title = "24 LuXuXu";
      int tw = u8g2.getStrWidth(title);
      u8g2.setCursor(64 - tw / 2, 26);
      u8g2.print(title);

      // 装饰线
      u8g2.drawHLine(10, 15, 108);
      u8g2.drawHLine(10, 40, 108);

      // 四角小装饰
      u8g2.drawPixel(4, 4);
      u8g2.drawPixel(123, 4);
      u8g2.drawPixel(4, 59);
      u8g2.drawPixel(123, 59);

      // 底部进度条
      int barPct = map(elapsed - 2200, 0, 800, 0, 100);
      if (barPct > 100) barPct = 100;
      u8g2.drawFrame(24, 48, 80, 6);
      if (barPct > 0) {
        u8g2.drawBox(25, 49, barPct * 78 / 100, 4);
      }

      char pctBuf[6];
      snprintf(pctBuf, sizeof(pctBuf), "%d%%", barPct);
      int pw = u8g2.getStrWidth(pctBuf);
      u8g2.setCursor(64 - pw / 2, 63);
      u8g2.print(pctBuf);
    }

    u8g2.sendBuffer();
  }
}
