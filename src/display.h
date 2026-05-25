#pragma once

void drawPage1();
void drawPage2();
void refreshCurrentPage();
void playBootAnimation();
void drawBarGauge(int y, const char* label, const char* value, float ratio, int barH);
void drawWeatherIcon(int x, int y, int code);
void drawPixelCheck(int cx, int cy);
void drawConnectingScreen(const char* title, const char* detail, const char* status, int pct);
void drawConnectedScreen(const char* title, const char* ip, int rssi);
