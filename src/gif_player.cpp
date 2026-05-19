#include "gif_player.h"
#include <string.h>

// ==================== LittleFS 初始化 ====================
bool initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS 挂载失败");
    return false;
  }
  Serial.println("[FS] LittleFS 已就绪");

  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("[FS] %s (%d bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }
  return true;
}

// ==================== AnimatedGIF 文件回调 ====================
static File gifFile;

static void *GIFOpenFile(const char *fname, int32_t *pSize) {
  gifFile = LittleFS.open(fname, "r");
  if (!gifFile) return NULL;
  *pSize = gifFile.size();
  return (void *)&gifFile;
}

static void GIFCloseFile(void *pHandle) {
  if (gifFile) gifFile.close();
}

static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  int32_t iBytesRead = iLen;
  if ((pFile->iSize - pFile->iPos) < iLen)
    iBytesRead = pFile->iSize - pFile->iPos;
  if (iBytesRead <= 0) return 0;
  int32_t iBytesActual = gifFile.read(pBuf, iBytesRead);
  pFile->iPos = gifFile.position();
  return iBytesActual;
}

static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  if (iPosition < 0) iPosition = 0;
  else if (iPosition >= pFile->iSize) iPosition = pFile->iSize - 1;
  gifFile.seek(iPosition);
  pFile->iPos = gifFile.position();
  return pFile->iPos;
}

// ==================== GIF 渲染回调 ====================
static U8G2* g_u8g2 = nullptr;

// 将 RGB565 调色板项转换为 0/1（亮度阈值），用于 1-bit OLED
// 阈值取中间灰度，亮像素（>=阈值）点亮
static inline uint8_t paletteToMono(uint16_t rgb565) {
  // RGB565 -> R/G/B 8-bit
  uint8_t r = (rgb565 >> 8) & 0xF8;
  uint8_t g = (rgb565 >> 3) & 0xFC;
  uint8_t b = (rgb565 << 3) & 0xF8;
  // 近似亮度 (Rec.601): Y = 0.299R + 0.587G + 0.114B
  uint16_t luma = (r * 77 + g * 150 + b * 29) >> 8;
  return luma >= 128 ? 1 : 0;
}

static void GIFDraw(GIFDRAW *pDraw) {
  if (!g_u8g2) return;

  // 每一帧的第一行：清空显示缓冲区，避免帧间像素叠加导致整屏变白
  if (pDraw->y == 0) {
    memset(g_u8g2->getBufferPtr(), 0, 128 * 64 / 8);
  }

  uint8_t  *pPixels  = pDraw->pPixels;
  uint16_t *pPalette = pDraw->pPalette;
  int y = pDraw->iY + pDraw->y;

  if (y >= 64 || y < 0) return;

  uint8_t *buf = g_u8g2->getBufferPtr();
  uint8_t hasTrans  = pDraw->ucHasTransparency;
  uint8_t transIdx  = pDraw->ucTransparent;
  int row = (y >> 3) * 128;
  uint8_t mask = (1 << (y & 7));

  for (int x = 0; x < pDraw->iWidth; x++) {
    int screenX = pDraw->iX + x;
    if ((unsigned)screenX >= 128) continue;

    uint8_t idx = pPixels[x];
    // 透明像素：保持背景（已被 memset 清零）
    if (hasTrans && idx == transIdx) continue;

    // 通过调色板亮度判断是否点亮 OLED 像素
    if (paletteToMono(pPalette[idx])) {
      buf[row + screenX] |= mask;
    }
  }
}

// ==================== 播放 GIF 开机动画 ====================
bool playGifBoot(U8G2 &u8g2, unsigned long duration, float speedFactor) {
  if (!LittleFS.exists(GIF_BOOT_PATH)) {
    Serial.printf("[GIF] 文件不存在: %s\n", GIF_BOOT_PATH);
    return false;
  }

  if (speedFactor < 0.1f) speedFactor = 0.1f;   // 防止除零 / 过慢
  if (speedFactor > 10.0f) speedFactor = 10.0f; // 上限，避免帧间无延迟

  AnimatedGIF *gif = new AnimatedGIF();
  gif->begin(BIG_ENDIAN_PIXELS);

  g_u8g2 = &u8g2;
  unsigned long startTime = millis();
  int frameCount = 0;

  if (gif->open(GIF_BOOT_PATH, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    Serial.printf("[GIF] 尺寸: %dx%d, 倍速: %.2fx\n",
                  gif->getCanvasWidth(), gif->getCanvasHeight(), speedFactor);

    while (millis() - startTime < duration) {
      // 不让库内部 sync 等待，自己按倍速重新计算帧延迟
      int frameDelayMs = 0;
      int rc = gif->playFrame(false, &frameDelayMs);
      if (rc == 0) {
        // 一轮播放结束，重置循环
        gif->reset();
        continue;
      }
      if (rc < 0) break; // 出错

      frameCount++;
      u8g2.sendBuffer();

      // 按倍速等待，但每 5ms yield 一次以保持系统响应
      unsigned long waitMs = (unsigned long)(frameDelayMs / speedFactor);
      unsigned long waitStart = millis();
      while (millis() - waitStart < waitMs) {
        if (millis() - startTime >= duration) break;
        delay(5);
        yield();
      }
    }

    gif->close();
    Serial.printf("[GIF] 播放结束，共 %d 帧\n", frameCount);
  } else {
    Serial.println("[GIF] 打开失败");
    delete gif;
    g_u8g2 = nullptr;
    return false;
  }

  delete gif;
  g_u8g2 = nullptr;
  return true;
}
