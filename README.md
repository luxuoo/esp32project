# ESP32 智能环境监测站

基于 ESP32 + OLED 的智能环境监测系统，支持温湿度、光照采集、天气查询、MQTT 远程控制。

## 硬件清单

| 组件 | 型号 | 用途 |
|------|------|------|
| 主控板 | ESP32 Feather | 数据处理 + 网络通信 |
| 屏幕 | SSD1306 OLED 128x64 | 显示传感器数据和天气 |
| 温湿度传感器 | DHT11 | 采集温度和湿度 |
| 光敏电阻 | GM-31 | 采集环境光照 |
| LED x4 | — | WiFi / MQTT / 错误 / 可调亮度指示 |
| 按键 | — | 手动刷新数据 |

## 功能

- **开机动画**：雷达扫描 + 打字机标题 + 加载阶段列表
- **WiFi 连接**：自动连接，带复古终端风格进度界面，D3 指示状态
- **MQTT 通信**：订阅远程指令，上报传感器和天气数据，D4 指示状态
- **环境监测**：每 5 秒自动采集温度、湿度、光照并显示
- **天气查询**：每 10 分钟从 Open-Meteo API 获取北京天气
- **手动刷新**：按下按键立即更新所有数据
- **LED 控制**：通过 MQTT 远程控制 D5 亮度（开/50%/关）

## 文件说明

| 文件 | 说明 |
|------|------|
| `src/main.cpp` | 正式版代码，精简注释 |
| `src/main_tutorial.cpp` | 小白讲解版，逐行中文注释，适合初学者阅读 |

## MQTT 主题

| 主题 | 方向 | 说明 |
|------|------|------|
| `esp32/req/sensor` | 下行 | 请求传感器数据 |
| `esp32/req/weather` | 下行 | 请求天气数据 |
| `esp32/ctrl/led` | 下行 | 控制 LED（LED_ON / LED_BRIGHT_50 / LED_OFF） |
| `esp32/resp/sensor` | 上行 | 传感器数据 JSON |
| `esp32/resp/weather` | 上行 | 天气数据 JSON |
| `esp32/req/manual` | 上行 | 手动刷新通知 |

## 编译与烧录

```bash
# 安装 PlatformIO 后
pio run            # 编译
pio run -t upload  # 烧录
pio device monitor # 打开串口监视器
```

## 依赖库

- [U8g2](https://github.com/olikraus/u8g2) — OLED 屏幕驱动
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) — JSON 解析
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT 客户端
- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library) — 温湿度传感器
