# Xiaozhi WiFi Audio Light（小智 WiFi 音乐氛围灯）

## 概述

基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT）二次开发的 **AI 音乐氛围灯** 开发板配置（ESP32-S3）。
在保留小智语音对话能力的基础上，新增 **实时音频可视化（FFT 频谱灯）**，并具备 **Web 控制台 / MCP 语音控制 / 局域网 OTA** 的完整产品形态。

## 特性

- **双模式互斥**：聊天模式（AI 在线，常驻氛围灯 + 情绪染色）/ 音乐模式（AI 暂停防误触，FFT 律动）。
- **音乐模式双音源**：电脑 UDP 推流（WASAPI 回环）/ 板载麦克风环境声。
- **实时音频可视化**：1024 点 FFT（16 kHz）→ 32 段对数频谱（20 Hz–8 kHz），绑核 core1 稳定 ~30fps。
- **18 种灯效**：节拍引擎（BPM/onset/脉冲）+ 后处理（gamma/噪声门/余晖/抖动）。
- **控制方式**：Web 控制台、语音（MCP）、TOUCH 键。
- **局域网 OTA**：`/api/ota`（含镜像确认回滚保护）。

## 硬件配置

### 音频（Simplex I2S）
| 功能 | 引脚 |
|---|---|
| 麦克风 I2S | WS=GPIO17, SCK=GPIO16, DIN=GPIO18（16 kHz） |
| 扬声器 I2S | DOUT=GPIO40, BCLK=GPIO39, LRCK=GPIO38（24 kHz） |

### 显示屏（SSD1306 128x64, I2C）
- SDA: GPIO12
- SCL: GPIO13

### WS2812 氛围灯带
- DIN: GPIO47，**30 灯**
- 供电：独立 5V/≥1A，与板**共地**，入口并联 **≥1000µF** 电容（否则易 brownout 重启）

### 按钮
- BOOT: GPIO0
- TOUCH: GPIO14（音乐模式下用于退出）

## MCP 语音控制工具

- `self.strip.set_mode` — 切换灯效（按名称）
- `self.strip.next_mode` — 切换到下一种灯效
- `self.strip.set_brightness` — 设置亮度（0–100%）
- `self.strip.set_auto_follow` — 开启/锁定自动跟随设备状态
- `self.strip.set_audio_source` — 音乐模式音源（wifi / mic）
- `self.strip.set_music_mode` — 进入/退出音乐模式
- `self.strip.get_status` — 查询状态（模式/亮度/音源/BPM 等）

## Web 控制台 / API

浏览器访问 `http://<设备IP>/`：

- 模式切换、灯效网格、FX/后处理参数、实时 32 段频谱
- 一键推流启动器：`/start.bat`（PC 端 `loopback.py`，WASAPI 回环 → UDP 5004）
- REST API：`/api/status | mode | brightness | command | auto | post | fx | spectrum | source | music | ota`

## 编译配置

- 目标芯片：**ESP32-S3**
- 框架：**ESP-IDF v6.1**
- 板类型：`CONFIG_BOARD_TYPE_XIAOZHI_WIFI_AUDIO_LIGHT`
- 关键开关（menuconfig → *OYC Xiaozhi WiFi Audio Light Options*）：
  - `OYC_DEV_MODE`（Web 控制台 + 局域网 OTA，量产可关）
  - `OYC_AMBIENT_LIGHT`
  - `OYC_STRIP_BRIGHTNESS`

```bash
idf.py set-target esp32s3
# menuconfig: Board Type -> Xiaozhi WiFi Audio Light
idf.py build
idf.py -p <PORT> flash monitor
```

## 版本信息

当前版本：1.0.0（`XIAOZHI_WIFI_AUDIO_LIGHT_VERSION`）
