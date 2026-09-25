# Xiaozhi WiFi Audio Light · 小智 WiFi 音乐氛围灯

一个基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT License）二次开发的 **AI 音乐氛围灯**：
保留小智的语音对话能力，再加上 **FFT 频谱律动 / 18 种灯效 / 电脑 Wi-Fi 推流 / 情绪灯 / Web 控制台 / 局域网 OTA**。

> 本项目为个人二次开发，上游版权归 xiaozhi-esp32 作者所有，遵循 MIT License（见 `LICENSE`）。

---

## 功能特性

- **双工作模式（互斥）**
  - **聊天模式**：小智语音在线；灯光 = 常驻氛围灯效（18 种可选）+ **情绪染色**（对话时随情绪变色）。
  - **音乐模式**：小智语音暂停（避免音乐误唤醒）；灯光 = **FFT 频谱律动**。
- **音乐模式音源可选**
  - **电脑推流**：PC 用 WASAPI 回环抓系统声音 → UDP 推给设备（16k 单声道）。
  - **环境声**：设备麦克风听环境声（此时由本工程自己读麦，不占用小智语音链路）。
- **音频可视化**：1024 点 FFT @16k，32 段对数频谱，统一节拍引擎（BPM/onset/脉冲）+ 后处理（gamma/噪声门/余晖）。
- **语音控制（MCP）**：`self.strip.*` —— 切换灯效/亮度/模式/音源、查询状态。
- **Web 控制台**：模式切换、灯效网格、效果参数、实时 32 段频谱、一键推流启动器、局域网 OTA。
- **局域网 OTA**：`curl -T build/xiaozhi.bin http://<设备IP>/api/ota`，免接线升级。

---

## 硬件（目前为面包板，自绘 PCB后续更新完善后补上，主板ESP32-S3）

| 功能 | 引脚 |
|---|---|
| 麦克风 I2S | WS=GPIO17, SCK=GPIO16, DIN=GPIO18（16 kHz） |
| 喇叭 I2S | DOUT=GPIO40, BCLK=GPIO39, LRCK=GPIO38（24 kHz） |
| OLED (SSD1306, I2C) | SDA=GPIO12, SCL=GPIO13 |
| WS2812 氛围灯 | DIN=GPIO47，30 灯 |
| 按键 | BOOT=GPIO0, TOUCH=GPIO14 |

> ⚠️ 灯带请用**稳定的 5V/≥1A 电源**并共地、入口并联 ≥1000µF 电容；供电不足会导致 brownout 重启。

---

## 构建

环境：**ESP-IDF v6.1**，目标芯片 **esp32s3**。

```bash
idf.py set-target esp32s3
# menuconfig:  Board Type -> "Xiaozhi WiFi Audio Light"
idf.py build
idf.py -p <PORT> flash monitor
```

> 首次需在 `menuconfig` 里选中本板；开发调试开关在 `OYC Xiaozhi WiFi Audio Light Options` 菜单：
> `OYC_DEV_MODE`（Web 控制台 + 局域网 OTA，量产可关）、`OYC_AMBIENT_LIGHT`、`OYC_STRIP_BRIGHTNESS`。

---

## 使用

### 1. 配网
开机未联网 → 进入配网热点 `Xiaozhi-xxxx`，浏览器打开 `192.168.4.1` 填写 Wi-Fi。

### 2. Web 控制台
浏览器访问 `http://<设备IP>/`：
- 工作模式：💬 聊天模式 / 🎵 音乐模式
- 音乐模式音源：📶 电脑推流 / 🎤 环境声
- 灯效网格（18 种）、后处理/FX 参数、实时频谱
- ⬇ 下载电脑启动器（一键推流）

### 3. 电脑推流（音乐模式·电脑推流）
浏览器访问 `http://<设备IP>/start.bat` 下载并双击运行 `start_esp_audio.bat`
（自动下载脚本、装 `pyaudiowpatch numpy`、抓系统声音推送）；或手动：
```bash
python loopback.py <设备IP> 5004
```

### 4. 语音
- "切换到彩虹模式" / "把氛围灯调成烟花" —— 切灯效
- "进入音乐模式" / "退出音乐模式" —— 切模式
- "用电脑的音乐" / "听环境声" —— 切音乐模式音源
- "氛围灯亮度调到 30%"

---

## 目录结构（本项目新增部分）

```
components/
  oyc_visualizer/     # 氛围灯引擎（FFT 分发 + 渲染任务 + 模式/情绪/音源状态机）
  oyc_web_console/    # Web 控制台 + API + 一键推流 + OTA 注册（assets/console.html, loopback.py）
  oyc_lan_ota/        # 局域网固件上传 /api/ota
  audio_processor/    # FFT / 32 对数频带（外部喂 PCM）
  led_controller/     # 18 种灯效 + 节拍引擎 + 后处理（RMT/led_strip）
  dual_core_com/      # 命令队列 + 状态快照（单一数据源总线）
  wifi_audio/         # UDP 收流 5004 + 设备发现 5005
  utils/              # 数学/DSP 辅助
  json/               # cJSON 依赖别名
main/boards/xiaozhi-wifi-audio-light/   # 板级定义（config.h / 板级 .cc / MCP 工具）
```

---

## 说明与致谢

- 上游：[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT），本项目在其基础上新增音频可视化与氛围灯能力。
- 音频可视化引擎移植自作者的 FFT 项目（`audio_processor` / `led_controller` / `dual_core_com` / `wifi_audio`）。
- 托管组件（`managed_components/`）不入库，由 `idf_component.yml` + `dependencies.lock` 自动获取。

License: MIT（见 `LICENSE`）。
