#include "display.h"
#include "globals.h"

void drawPage1() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, 12);
  u8g2.print("D5=");
  u8g2.print(d5_brightness);
  u8g2.print("%");
  u8g2.setCursor(80, 12);
  u8g2.print("[1/2]");
  u8g2.drawLine(0, 14, 128, 14);

  u8g2.setCursor(0, 30);
  if (sensor_error) {
    u8g2.print("温度: -- C");
    u8g2.setCursor(0, 45); u8g2.print("湿度: -- %RH");
    u8g2.setCursor(0, 60); u8g2.print("光照: -- 级");
  } else {
    u8g2.print("温度: "); u8g2.print(temp, 1); u8g2.print(" C");
    u8g2.setCursor(0, 45);
    u8g2.print("湿度: "); u8g2.print(hum, 1); u8g2.print(" %RH");
    u8g2.setCursor(0, 60);
    u8g2.print("光照: "); u8g2.print(light_level); u8g2.print(" 级");
  }
  u8g2.sendBuffer();
}

void drawPage2() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setCursor(0, 12);
  u8g2.print("[2/2] 天气信息");
  u8g2.drawLine(0, 14, 128, 14);

  if (weather_fetch_failed) {
    u8g2.setCursor(0, 35);
    u8g2.print("天气获取失败");
  } else if (weather_available) {
    u8g2.setCursor(0, 30); u8g2.print("城市: "); u8g2.print(weather_city);
    u8g2.setCursor(0, 45); u8g2.print("天气: "); u8g2.print(weather_text);
    u8g2.setCursor(0, 60); u8g2.print("温度: "); u8g2.print(weather_temp_str); u8g2.print(" C");
  } else {
    u8g2.setCursor(0, 35);
    u8g2.print("正在获取天气...");
  }
  u8g2.sendBuffer();
}

void refreshCurrentPage() {
  if (currentPage == 1) drawPage1();
  else                  drawPage2();
}

// cx, cy = 小人脚底中心坐标
// frame  = 当前帧 (0~3 循环)
void drawRunner(int cx, int cy, int frame) {
  int bounce = (frame == 1 || frame == 3) ? -1 : 0;
  cy += bounce;

  u8g2.drawDisc(cx, cy - 23, 3);
  u8g2.drawLine(cx, cy - 20, cx, cy - 8);

  switch (frame % 4) {
    case 0:
      u8g2.drawLine(cx, cy - 17, cx - 5, cy - 12);
      u8g2.drawLine(cx, cy - 17, cx + 5, cy - 12);
      break;
    case 1:
      u8g2.drawLine(cx, cy - 17, cx + 9, cy - 13);
      u8g2.drawLine(cx, cy - 17, cx - 6, cy - 11);
      break;
    case 2:
      u8g2.drawLine(cx, cy - 17, cx - 4, cy - 12);
      u8g2.drawLine(cx, cy - 17, cx + 4, cy - 12);
      break;
    case 3:
      u8g2.drawLine(cx, cy - 17, cx - 9, cy - 13);
      u8g2.drawLine(cx, cy - 17, cx + 6, cy - 11);
      break;
  }

  switch (frame % 4) {
    case 0:
      u8g2.drawLine(cx, cy - 8, cx - 3, cy);
      u8g2.drawLine(cx, cy - 8, cx + 3, cy);
      break;
    case 1:
      u8g2.drawLine(cx, cy - 8, cx + 9, cy);
      u8g2.drawLine(cx, cy - 8, cx - 6, cy - 3);
      break;
    case 2:
      u8g2.drawLine(cx, cy - 8, cx - 2, cy);
      u8g2.drawLine(cx, cy - 8, cx + 2, cy);
      break;
    case 3:
      u8g2.drawLine(cx, cy - 8, cx - 9, cy);
      u8g2.drawLine(cx, cy - 8, cx + 6, cy - 3);
      break;
  }

  if (frame == 0 || frame == 2) {
    u8g2.drawPixel(cx - 4, cy + 1);
    u8g2.drawPixel(cx + 4, cy + 1);
  }
}

void playBootAnimation() {
  unsigned long startTime = millis();

  while (millis() - startTime < 3000) {
    unsigned long elapsed = millis() - startTime;
    int progress = map(elapsed, 0, 3000, 0, 100);
    int frame    = (elapsed / 120) % 4;

    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setCursor(28, 10);
    u8g2.print("24 LuXuXu");
    u8g2.drawHLine(0, 13, 128);

    int dashOff = (elapsed / 40) % 10;
    for (int i = 0; i < 3; i++) {
      int lx = 18 - dashOff - i * 7;
      if (lx > 2 && lx < 32) {
        u8g2.drawHLine(lx, 24 + i * 6, 6);
      }
    }

    drawRunner(55, 42, frame);

    int gndOff = (elapsed / 40) % 16;
    for (int x = -gndOff; x < 128; x += 16) {
      u8g2.drawHLine(x, 44, 8);
    }

    u8g2.drawFrame(14, 49, 100, 10);
    int barW = map(progress, 0, 100, 0, 98);
    if (barW > 0) {
      u8g2.drawBox(15, 50, barW, 8);
    }

    u8g2.setCursor(54, 63);
    u8g2.print(progress);
    u8g2.print("%");

    u8g2.sendBuffer();
  }
}
