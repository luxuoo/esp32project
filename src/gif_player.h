#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <U8g2lib.h>

// GIF 文件路径（放在 LittleFS 根目录）
#define GIF_BOOT_PATH "/boot.gif"

// 初始化 LittleFS 文件系统
bool initLittleFS();

// 播放 GIF 开机动画
// duration: 播放时长（毫秒），到时间后自动返回
// speedFactor: 倍速因子，1.0 = 原速，2.0 = 双倍速，0.5 = 半速
// 返回 true 表示成功播放，false 表示文件不存在或解码失败
bool playGifBoot(U8G2 &u8g2, unsigned long duration = 3000, float speedFactor = 1.0f);
