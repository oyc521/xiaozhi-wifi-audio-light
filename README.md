# Xiaozhi WiFi Audio Light · 小智 WiFi 音乐氛围灯

一个基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT License）二次开发的 **AI 音乐氛围灯**：
在保留小智 **语音对话** 能力的基础上，新增 **实时音频可视化（FFT 频谱灯）**，并具备 **Web 控制台 / 语音控制 / 局域网 OTA** 的完整产品形态。

> 本项目为个人二次开发：**上游框架为开源项目**，其中的音频可视化引擎、灯效、Web 控制台、OTA、脚本等 **为本项目自行实现**。上游版权归 xiaozhi-esp32 作者所有，遵循 MIT License（见 `LICENSE`）。

---

## 项目简介

把一台纯语音助手做成"能听歌律动"的桌面灯：既保留语音交互，又实现**实时音频可视化**，并提供可网页控制、可 OTA 升级的完整固件。硬件为自绘 **ESP32-S3 PCB**（I2S 麦克风/喇叭 + 30 颗 WS2812 + SSD1306 OLED）。

**核心能力一句话**：语音对话 / 音乐律动 / 稳定氛围灯 **三模式互斥**；音源可 PC 推流或板载麦；灯光由 FFT 频谱与节拍实时驱动。

## 项目亮点

- **实时音频可视化**：esp-dsp 硬件加速 **1024 点 FFT**（16 kHz）→ **32 段对数频谱**（20 Hz–8 kHz），渲染任务**绑核 core1**、稳定 **~30 fps** 驱动 30 颗 WS2812。
- **双核解耦架构**：以「**命令队列 + 状态快照**」单数据源总线解耦生产者/消费者（音频、灯效、网络、UI 互不阻塞）。
- **三模式互斥状态机**：聊天（AI 在线 + 情绪氛围灯）/ 音乐（FFT 律动，**保留唤醒词**可语音打断）/ **氛围灯**（确定性慢 LFO 合成信号，稳定渲染、不吃音频）。
- **双音源**：电脑 **UDP 推流**（WASAPI 回环）/ 板载**环境声**麦克风。
- **单色 OLED 像素表情**：自绘 1-bit 像素脸（13 情绪 × 睁/闭眼、**64/48 两套**），待机大脸铺满、系统态显示状态栏。
- **性能/内存优化**：定位修复**任务优先级饿死音频**、**opus 栈溢出**；大任务栈搬 **PSRAM**（内部 SRAM 13K→63K）；解决 **WiFi 省电导致的频谱卡顿** 与 **OTA 被拖慢**。
- **全链路产品化**：Web 控制台、MCP 语音控制、**局域网 OTA（镜像确认回滚保护）**。

## 技术栈

| 层 | 技术 |
|---|---|
| 芯片/RTOS | ESP32-S3（双核 240MHz）· FreeRTOS |
| 框架/构建 | ESP-IDF **v6.1** · CMake · 组件化管理 |
| DSP | esp-dsp（`dsps_fft2r_fc32` 浮点 FFT） |
| 显示/灯效 | SSD1306 128×64 单色（esp_lcd · LVGL，自绘像素表情）· WS2812×30（RMT / `led_strip`） |
| 网络/协议 | Wi-Fi · UDP（收流/发现）· httpd(REST) · MQTT · MCP(JSON-RPC) |
| 升级 | esp_ota 双分区 + 镜像确认 |

---

## 系统架构

```
┌────────────── 小智框架层 ──────────────┐
│ Application(状态机) / Board(板级)      │
│   · XiaozhiOledDisplay : OledDisplay  │  ← 覆写 SetEmotion 做情绪灯
│   · AudioCodec / Display / MCP Server │
├────────────── 本项目组件层 ────────────┤
│ oyc_visualizer   引擎大脑：三模式/音源/情绪/渲染任务
│   ├─ audio_processor   FFT + 32 对数频带        
│   ├─ led_controller    18 灯效 + 节拍 + 后处理  
│   ├─ dual_core_com     命令队列 + 状态快照总线   
│   └─ wifi_audio        UDP 5004 收流 / 5005 发现
│ oyc_web_console  httpd 控制台 + REST API + 一键推流
│ oyc_lan_ota      /api/ota 局域网升级             
│ 板级 oyc_faces.c  1-bit 像素表情（64/48，脚本生成）
```

### 数据流

```
音源 ──┐  PC UDP:5004 ─► wifi_audio_rx(core0)  ─┐
       └  板载麦      ─► OycAmbientMicTask     ─┤
                                                ▼
                                  环形缓冲(int16) ──► oyc_vis_task (core1, ~30fps)
                                                           │  取 1024 点帧
                                                        [FFT 1024 @16k]
                                                           │
                                            32 对数频带能量(+平滑) + 节拍引擎
                                                           │
                                     led_controller(18 效果) ─► 后处理 ─► RMT ─► WS2812
                                                           │
                                          dual_core_com 状态快照 ─► Web API / MCP 查询
```

---

## 关键设计（技术要点）

- **FFT 参数取舍**：1024 点 @16 kHz → 分辨率 **15.625 Hz**、帧长约 64 ms，是"低频分辨力 vs 延迟"的折中；用 **bin→band 连续映射**（每段≥1 bin、跳过 DC）避免低频"死带"。
- **实时性**：渲染任务 `pinned core1`、优先级 **3**（**低于音频 AFE 的 5**，避免渲染饿死音频导致 `AFE(FEED) is full`），core0 留给 Wi-Fi/协议栈，降低抖动。
- **解耦**：跨核用**队列**（命令）+ **快照**（状态），避免共享内存加锁与优先级反转。
- **三模式互斥**：单一状态源 + 守卫任务（200ms）；音乐模式**保留唤醒词**、仅在"空闲->对话"跳变时退出（避免语音进音乐模式秒退）；氛围模式用确定性 LFO 合成频谱；推流上升沿（仅聊天模式）自动进入，TOUCH/语音/网页退出。
- **内存/性能**：大任务栈搬 **PSRAM**（`xTaskCreateWithCaps`），WiFi/lwIP/IPC 等留内部；音乐模式与 OTA 上传期间关闭 WiFi 省电。
- **音乐联动**：统一**节拍引擎**（bass/mid/high、flux、onset、BPM、脉冲），供各效果共享；爆炸踏鼓点、星空色团、对撞节奏脉冲、频谱跳动等按能量/节拍驱动。
- **视觉后处理**：gamma → 噪声门（随亮度缩放、逐通道去彩噪）→ 余晖 → 8-bit 有序抖动（去低亮度色带）。
- **调参平滑**：亮度 / FX / 后处理参数均"目标值 + 每帧缓动"，色相走最短弧；网络流做**活性检测**区分"静音/无数据"。
- **升级安全**：esp_ota 双分区 + 开机**镜像确认**（未确认自动回滚）。

---

## 功能特性

- **三工作模式（互斥）**
  - **聊天模式**：小智语音在线；灯光 = 常驻氛围灯效（18 种）+ **情绪染色**（对话随情绪变色）。
  - **音乐模式**：ASR 暂停但**保留唤醒词**（说"你好小智"即打断退出）；灯光 = **FFT 频谱律动**。
  - **氛围灯模式**：纯渲染，用**确定性慢 LFO 合成频谱**驱动，**不吃音频、不随环境声影响**，灯效稳定。
- **音乐模式音源**：电脑推流 / 环境声（二选一）。
- **OLED 像素表情**：13 情绪，待机/聆听/说话显示 **64×64 大脸**，初始化/连接等系统态显示状态栏 + 48×48 小脸；眨眼动画。
- **18 种灯效** + 节拍引擎 + 后处理。
- **控制方式**：Web 控制台 · 语音（MCP）· TOUCH 键。
- **局域网 OTA**：`curl -T build/xiaozhi.bin http://<设备IP>/api/ota`（上传前建议停推流/关网页，固件已自动关 WiFi 省电提速）。

---

## 硬件（目前为面包板，自绘 PCB 后续更新完善后补上，主板 ESP32-S3）

| 功能 | 引脚 |
|---|---|
| 麦克风 I2S | WS=GPIO17, SCK=GPIO16, DIN=GPIO18（16 kHz） |
| 喇叭 I2S | DOUT=GPIO40, BCLK=GPIO39, LRCK=GPIO38（24 kHz） |
| OLED (SSD1306 128×64, I2C) | SDA=GPIO12, SCL=GPIO13 |
| WS2812 氛围灯 | DIN=GPIO47，30 灯 |
| 按键 | BOOT=GPIO0, TOUCH=GPIO14 |

> ⚠️ 灯带请用**稳定的 5V/≥1A** 电源并**共地**、入口并联 **≥1000µF** 电容；供电不足会导致 brownout 重启。

---

## 构建

环境：**ESP-IDF v6.1**，目标芯片 **esp32s3**。

```bash
idf.py set-target esp32s3
# menuconfig:  Board Type -> "Xiaozhi WiFi Audio Light"
idf.py build
idf.py -p <PORT> flash monitor
```

> 首次需在 `menuconfig` 选中本板；开关在 *OYC Xiaozhi WiFi Audio Light Options*：
> `OYC_DEV_MODE`（Web 控制台 + 局域网 OTA，量产可关）、`OYC_AMBIENT_LIGHT`、`OYC_STRIP_BRIGHTNESS`、`OYC_MEM_DIAG`（内存诊断日志，默认关）。

---

## 使用

### 1. 配网
开机未联网 → 进入配网热点 `Xiaozhi-xxxx`，浏览器打开 `192.168.4.1` 填写 Wi-Fi。

### 2. Web 控制台
浏览器访问 `http://<设备IP>/`：模式切换、音乐模式音源、灯效网格（18 种）、FX/后处理参数、实时频谱、一键推流启动器。

### 3. 电脑推流（音乐模式 · 电脑推流）
- **一键（推荐）**：网页点「下载电脑启动器」（或访问 `http://<设备IP>/start.bat`）得到 `start_esp_audio.bat`，双击运行——自动装 `pyaudiowpatch numpy`、下载并运行 `stream_agent.py`。进音乐模式自动开始推流、退出自动停；网页可填「自动播放目标」（歌单/网址/App）实现自动放歌。
- **手动**：
  ```powershell
  python stream_agent.py <设备IP> [--play "F:\QQMusic\QQMusic.exe"] [--silence-exit 30]
  ```
- `stream_agent.py` 常驻守护：轮询 `/api/status`，检测到「音乐模式」自动开始/停止推流；**静音超 30s 自动停推**（设备随后自动退出音乐模式）；并打印**实测发送采样率**（应 ≈16000 样本/秒）。
- 备选 `loopback.py`：`python loopback.py <设备IP> 5004`（启动即一直推流）。

### 4. 语音
- "切换到彩虹模式" / "把氛围灯调成烟花" —— 切灯效
- "进入音乐模式" / "退出音乐模式" —— 切模式
- "用电脑的音乐" / "听环境声" —— 切音乐模式音源
- "氛围灯亮度调到 30%"

---

## Web API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET/POST | `/api/status` | 状态（模式/亮度/音源/BPM/内存/版本） |
| GET/POST | `/api/mode` | 读写灯效模式 |
| GET/POST | `/api/brightness` | 读写亮度（0–100） |
| GET/POST | `/api/fx` | 统一效果参数（speed/intensity/sensitivity/hue/color_speed/beat_react） |
| GET/POST | `/api/post` | 后处理（gamma/gate/afterimage） |
| GET/POST | `/api/source` | 音源（mic / wifi） |
| GET/POST | `/api/music` | 进入/退出音乐模式 |
| GET/POST | `/api/visual` | 三档视觉模式（chat / music / ambient） |
| GET/POST | `/api/auto` | 自动跟随开关 |
| GET | `/api/spectrum` | 实时 32 段频谱 |
| POST/PUT/GET | `/api/ota` | 局域网固件上传 |
| GET | `/start.bat` · `/stream_agent.py` · `/loopback.py` | 一键推流启动器与脚本下载 |

## MCP 语音工具

`self.strip.set_mode` · `next_mode` · `set_brightness` · `set_auto_follow` · `set_audio_source` · `set_music_mode` · `set_ambient_mode` · `get_status`

---

## 目录结构（本项目新增部分）

```
components/
  oyc_visualizer/     # 氛围灯引擎（三模式状态机 / FFT 分发 / 渲染任务 / 情绪 / 音源）
  oyc_web_console/    # Web 控制台 + API + 一键推流 + OTA 注册
                      #   assets/console.html, loopback.py, stream_agent.py
  oyc_lan_ota/        # 局域网固件上传 /api/ota
  audio_processor/    # FFT / 32 对数频带（外部喂 PCM）
  led_controller/     # 18 种灯效 + 节拍引擎 + 后处理（RMT/led_strip）
  dual_core_com/      # 命令队列 + 状态快照（单一数据源总线）
  wifi_audio/         # UDP 收流 5004 + 设备发现 5005
  utils/              # 数学/DSP 辅助
  json/               # cJSON 依赖别名
main/boards/xiaozhi-wifi-audio-light/   # 板级定义（config.h / 板级 .cc / MCP 工具）
                      #   oyc_faces.c/.h   1-bit 像素表情（脚本生成产物）
                      #   oyc_strip_mcp.cc MCP 工具注册
scripts/              # 表情美术工具：
                      #   gen_oyc_faces.py（生成 64/48 表情）/ make_xiaoai_faces.py（小爱专用）
                      #   import_oyc_faces_png.py（外部 PNG 覆盖）/ render_faces_c.py（从固件渲染预览）
```

---

## 说明与致谢

- 上游：[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT）—— 语音框架、板级/显示/MCP/协议层。
- **本项目实现**：音频可视化引擎、三模式状态机、灯效与节拍/后处理、双核通信、UDP 收流、Web 控制台、局域网 OTA、OLED 像素表情、电脑端推流/联动脚本、板级适配与 IDF6.1 迁移，以及音频/内存/性能排障优化。
- 托管组件（`managed_components/`）不入库，由 `idf_component.yml` + `dependencies.lock` 自动获取。

License: MIT（见 `LICENSE`）。
