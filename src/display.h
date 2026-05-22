#pragma once

void drawPage1();
void drawPage2();
void refreshCurrentPage();
void playBootAnimation();
void drawGauge(int cx, int cy, int r, float value, float minVal, float maxVal,
               const char* label, const char* unit, int decimals);
void drawLightBar(int x, int y, int w, int h, int value, int maxVal);
void drawWeatherIcon(int x, int y, int code);
void drawPixelCheck(int cx, int cy);
void drawConnectingScreen(const char* title, const char* detail, const char* status, int pct);
void drawConnectedScreen(const char* title, const char* ip, int rssi);
