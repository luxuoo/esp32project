# ESP32 智能环境监测站

基于 ESP32 的智能环境监测系统，集成温湿度采集、光照检测、天气查询、OLED 显示和 MQTT 远程控制。

## 功能特性

- **环境监测**：通过 DHT11 采集温湿度，GM31 光敏传感器采集光照强度
- **OLED 双页显示**：
  - 第 1 页：LED 亮度、温度、湿度、光照值
  - 第 2 页：城市、天气状况（中文）、气温（来自 Open-Meteo API）
- **开机动画**：支持自定义 GIF 动画（LittleFS），未配置时使用默认走路小人动画
- **MQTT 远程控制**：支持远程读取传感器数据、获取天气、控制 LED 亮度
- **手动刷新**：按下按钮即可立即刷新全部数据
- **状态指示灯**：4 颗 LED 分别指示 WiFi / MQTT 连接状态、错误通知
- **自动重连**：WiFi 和 MQTT 断线后自动重连
- **定时任务**：每 5 秒采集传感器数据，每 10 分钟更新天气信息

## 硬件清单

| 组件 | 型号/规格 | 说明 |
|------|-----------|------|
| 主控板 | Adafruit Feather ESP32 | 主处理器 |
| 显示屏 | SSD1306 128×64 OLED | I2C 接口 |
| 温湿度传感器 | DHT11 | 数字接口 |
| 光敏传感器 | GM31 | 模拟输出 |
| LED × 4 | — | WiFi/MQTT 状态 + PWM 亮度控制 + 错误通知 |
| 按钮 × 1 | — | 手动刷新 |

## 引脚定义

```
┌──────────────┬────────┬────────────────────────┐
│     名称     │  GPIO  │         功能           │
├──────────────┼────────┼────────────────────────┤
│ PIN_D3       │  14    │ WiFi 状态 LED          │
│ PIN_D4       │  27    │ MQTT 状态 LED          │
│ PIN_D5       │  26    │ PWM 亮度控制 LED       │
│ PIN_D6       │  33    │ 错误/通知 LED          │
│ PIN_SW1      │  32    │ 手动刷新按钮           │
│ PIN_OLED_SDA │  21    │ OLED I2C 数据线        │
│ PIN_OLED_SCL │  22    │ OLED I2C 时钟线        │
│ DHTPIN       │   4    │ DHT11 数据引脚         │
│ PIN_GM31     │  35    │ GM31 光敏传感器 (ADC)  │
└──────────────┴────────┴────────────────────────┘
```

## MQTT 主题

### 订阅（设备接收）

| 主题 | 消息 | 说明 |
|------|------|------|
| `esp32/req/sensor` | 任意 | 触发传感器读取，结果发布到 `esp32/resp/sensor` |
| `esp32/req/weather` | 任意 | 触发天气查询，结果发布到 `esp32/resp/weather` |
| `esp32/ctrl/led` | `LED_ON` / `LED_BRIGHT_50` / `LED_OFF` | 控制 D5 LED 亮度 |

### 发布（设备发送）

| 主题 | 格式 | 说明 |
|------|------|------|
| `esp32/resp/sensor` | `{"temp": float, "hum": float, "light": int}` | 传感器数据 JSON |
| `esp32/resp/weather` | `{"city": string, "weather": string, "temp": string, "code": int}` | 天气数据 JSON |
| `esp32/req/manual` | `"手动刷新成功"` | 按钮按下后通知 |

## 开发环境

- **框架**：Arduino (PlatformIO)
- **开发板**：Adafruit Feather ESP32

### 依赖库

| 库名 | 版本 | 用途 |
|------|------|------|
| [U8g2](https://github.com/olikraus/u8g2) | ^2.36.18 | SSD1306 OLED 驱动 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.2.2 | JSON 序列化/反序列化 |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | ^2.8 | MQTT 客户端 |
| [DHT sensor library](https://github.com/adafruit/DHT-sensor-library) | ^1.4.7 | DHT11 温湿度传感器驱动 |
| [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) | ^2.2.2 | GIF 动画解码（开机动画） |

## 快速开始

### 1. 克隆项目

```bash
git clone https://github.com/你的用户名/ESP32_U8g2.git
cd ESP32_U8g2
```

### 2. 修改配置

复制配置模板并填入你自己的信息：

```bash
cp src/config.example.h src/config.h
```

编辑 [src/config.h](src/config.h)，替换 WiFi 和 MQTT 信息：

```cpp
// WiFi 配置
#define WIFI_SSID     "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"

// MQTT 配置
#define MQTT_SERVER "你的MQTT服务器地址"
#define MQTT_PORT   1883
#define MQTT_USER   "你的MQTT用户名"
#define MQTT_PASS   "你的MQTT密码"
```

如需修改天气定位城市，修改 `WEATHER_API_URL` 中的经纬度坐标。

### 3. 编译并烧录

```bash
# 确保已安装 PlatformIO CLI
pio run

# 烧录到开发板
pio run --target upload
```

### 4. 查看串口日志

```bash
pio device monitor
```

### 5. 自定义 GIF 开机动画（可选）

将你的 GIF 文件命名为 `boot.gif`，放入 `data/` 目录，然后烧录到 LittleFS：

```bash
# 烧录 LittleFS 文件系统（将 data/ 目录内容写入 ESP32）
pio run --target uploadfs
```

**GIF 要求**：
- 建议尺寸：128×64 像素（或更小，居中显示）
- 格式：标准 GIF87a/GIF89a，支持动画
- 颜色：支持彩色，自动按亮度转为黑白（阈值 128）
- 支持透明色，透明区域显示为黑色背景
- 文件不宜过大，建议 < 200KB（LittleFS 可用空间约 896KB）
- 如果不放 `boot.gif`，自动使用默认的走路小人动画

播放参数可在 [src/main.cpp](src/main.cpp) 中调整：
- 时长（默认 4000ms）、倍速（默认 3.5x）

## 项目结构

```
data/
└── boot.gif          # 开机动画 GIF 文件（可选，烧录到 LittleFS）

src/
├── config.example.h  # 配置模板（无敏感信息，可安全提交）
├── config.h          # 本地配置（已 gitignore，需自行创建）
├── globals.h/.cpp    # 全局变量与对象
├── main.cpp          # 主程序入口与主循环
├── display.h/.cpp    # OLED 显示逻辑（双页切换、走路小人动画）
├── gif_player.h/.cpp # LittleFS + AnimatedGIF 开机动画播放
├── sensors.h/.cpp    # DHT11 + 光敏传感器读取
├── weather.h/.cpp    # Open-Meteo 天气 API 请求与解析
├── mqtt_handler.h/.cpp  # MQTT 连接、订阅、消息处理
└── d6_blink.h/.cpp   # D6 LED 闪烁通知（非阻塞）
```

## 工作流程

```
开机 → 开机动画(3s) → WiFi连接(30s超时) → MQTT连接(10次重试)
                                                      ↓
                                              获取初始天气数据
                                                      ↓
                            ┌─── 每5秒：读取传感器 → 更新显示 + MQTT发布
                            ├─── 每10分钟：获取天气 → 更新显示 + MQTT发布
                            ├─── 按钮按下：手动刷新全部数据
                            └─── MQTT指令：远程读取传感器/天气/控制LED
```

## 安全提示

> `src/config.h` 已被 `.gitignore` 排除，不会提交到仓库。  
> 本地需要自行创建 `config.h`（可从 `config.example.h` 复制），填入真实的 WiFi 和 MQTT 凭据。

## 许可证

MIT License
