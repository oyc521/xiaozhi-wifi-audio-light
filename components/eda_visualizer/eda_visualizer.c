/*
 * eda_visualizer —— AI 音乐氛围灯引擎（xiaozhi-wifi-audio-light 专用）
 *
 * 数据流（单任务，低优先级）：
 *   小智麦克风 tap -> audio_processor 环形缓冲 -> FFT(512@16k, 8带)
 *     -> led_controller 统一节拍引擎 + 18 种灯效 + 后处理
 *     -> WS2812 (回收的舵机 GPIO, led_strip/RMT)
 *
 * 单一数据源纪律：外部（Web 控制台 / MCP / 情绪钩子）一律通过
 * dual_core_com 命令队列修改参数，渲染任务内串行消费，无锁无竞态。
 */
#include "eda_visualizer.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "audio_processor.h"
#include "dual_core_com.h"
#include "wifi_audio.h"

static const char *TAG = "EDA_VIS";

#define VIS_FPS_MS 33   // ~30fps，与 A 仓库一致（512@16k=31.25 帧/s 数据率）
#define WIFI_AUDIO_UDP_PORT 5004

static fft_processor_t s_fft;
static bool s_started = false;
static led_mode_t s_current_mode = MODE_SPECTRUM;
static uint32_t s_frame_count = 0;
static bool s_auto_follow = true;
static eda_audio_src_t s_audio_src = EDA_AUDIO_SRC_MIC;
static bool s_wifi_rx_up = false;
static int16_t s_wifi_frame[FFT_SIZE];
static bool s_music_mode = false;
static eda_ai_audio_cb_t s_ai_cb = NULL;
static int64_t s_stream_lost_us = 0;
static bool s_prev_streaming = false;
static float s_silent_bands[NUM_FREQ_BANDS];   // 聊天模式静音频带（情绪场景）

// ---------------- 命令投递 ----------------

static void post_command(const core_command_t *cmd) {
    dual_core_com_send_command((core_command_t *)cmd, pdMS_TO_TICKS(50));
}

// 用户显式指定模式：同时关闭自动跟随，状态切换不再覆盖
void eda_visualizer_set_mode(led_mode_t mode) {
    s_auto_follow = false;
    core_command_t cmd = { .type = CMD_MODE_CHANGE };
    cmd.data.mode = mode;
    post_command(&cmd);
}

// 状态驱动的模式切换：仅在自动跟随开启时生效
void eda_visualizer_set_mode_auto(led_mode_t mode) {
    if (!s_auto_follow) return;
    core_command_t cmd = { .type = CMD_MODE_CHANGE };
    cmd.data.mode = mode;
    post_command(&cmd);
}

void eda_visualizer_set_auto_follow(bool follow) {
    s_auto_follow = follow;
    ESP_LOGI(TAG, "自动跟随设备状态: %s", follow ? "开" : "关");
}

bool eda_visualizer_is_auto_follow(void) {
    return s_auto_follow;
}

void eda_visualizer_set_brightness(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    core_command_t cmd = { .type = CMD_BRIGHTNESS_SET };
    cmd.data.param.value = percent;
    post_command(&cmd);
}

void eda_visualizer_set_post(float gamma, uint8_t noise_gate, float afterimage) {
    core_command_t cmd = { .type = CMD_POST_SET };
    cmd.data.post.gamma = gamma;
    cmd.data.post.gate = noise_gate;
    cmd.data.post.afterimage = afterimage;
    post_command(&cmd);
}

void eda_visualizer_set_fx(const led_fx_t *fx) {
    core_command_t cmd = { .type = CMD_FX_SET };
    cmd.data.fx.speed = fx->speed;
    cmd.data.fx.intensity = fx->intensity;
    cmd.data.fx.sensitivity = fx->sensitivity;
    cmd.data.fx.hue = fx->hue;
    cmd.data.fx.color_speed = fx->color_speed;
    cmd.data.fx.beat_react = fx->beat_react;
    post_command(&cmd);
}

void eda_visualizer_get_spectrum(uint8_t *out32) {
    led_get_spectrum(out32, 32);
}

// 情绪 -> (灯效 + 色相 + 强度) 映射。聊天模式用它驱动；音乐模式忽略。
void eda_visualizer_set_emotion(const char *emotion) {
    if (!emotion || s_music_mode) return;   // 音乐模式优先

    led_mode_t mode = MODE_AURORA;
    float hue = 0.09f, intensity = 0.9f;
    if      (strcmp(emotion, "happy") == 0)     { mode = MODE_AURORA; hue = 0.08f; intensity = 1.3f; }
    else if (strcmp(emotion, "laughing") == 0)  { mode = MODE_AURORA; hue = 0.10f; intensity = 1.4f; }
    else if (strcmp(emotion, "sad") == 0)       { mode = MODE_AURORA; hue = 0.62f; intensity = 0.75f; }
    else if (strcmp(emotion, "angry") == 0)     { mode = MODE_AURORA; hue = 0.98f; intensity = 1.6f; }
    else if (strcmp(emotion, "thinking") == 0)  { mode = MODE_AURORA; hue = 0.50f; intensity = 1.0f; }
    else if (strcmp(emotion, "surprised") == 0) { mode = MODE_AURORA; hue = 0.13f; intensity = 1.5f; }
    else if (strcmp(emotion, "cool") == 0)      { mode = MODE_AURORA; hue = 0.55f; intensity = 1.1f; }
    else                                        { mode = MODE_AURORA; hue = 0.09f; intensity = 0.9f; } // neutral

    eda_visualizer_set_mode(mode);   // 锁定灯效（不被状态切换覆盖）
    led_fx_t fx = *led_get_fx();
    fx.hue = hue;
    fx.intensity = intensity;
    eda_visualizer_set_fx(&fx);
    ESP_LOGD(TAG, "情绪灯: %s -> mode=%d hue=%.2f", emotion, (int)mode, hue);
}

led_mode_t eda_visualizer_get_mode(void) {
    return s_current_mode;
}

int eda_visualizer_get_brightness(void) {
    return led_get_brightness();
}

int eda_visualizer_get_bpm(void) {
    return led_get_beat()->bpm;
}

void eda_visualizer_set_audio_source(eda_audio_src_t src) {
    if (src == EDA_AUDIO_SRC_WIFI && !s_wifi_rx_up) {
        if (wifi_audio_init(WIFI_AUDIO_UDP_PORT) != ESP_OK) {
            ESP_LOGE(TAG, "UDP 接收器启动失败，源保持麦克风");
            return;
        }
        s_wifi_rx_up = true;
    }
    s_audio_src = src;
    s_fft.sample_rate = 16000;   // 麦克风与 WiFi 推流统一 16k（频带上限 8kHz）
    ESP_LOGI(TAG, "音频源切换: %s (端口 %d)",
             src == EDA_AUDIO_SRC_WIFI ? "WiFi 推流" : "麦克风", WIFI_AUDIO_UDP_PORT);
}

eda_audio_src_t eda_visualizer_get_audio_source(void) {
    return s_audio_src;
}

bool eda_visualizer_audio_streaming(void) {
    return s_audio_src == EDA_AUDIO_SRC_WIFI && wifi_audio_streaming();
}

// ---------------- 音乐模式（AI 对话 <-> 音乐可视化 互斥） ----------------
#define MUSIC_AUTO_EXIT_MS 30000   // 推流中断超过 30s 自动退出音乐模式

void eda_visualizer_set_ai_audio_cb(eda_ai_audio_cb_t cb) {
    s_ai_cb = cb;
}

bool eda_visualizer_is_music_mode(void) {
    return s_music_mode;
}

esp_err_t eda_visualizer_enter_music_mode(void) {
    if (s_music_mode) return ESP_OK;
    if (!s_started) return ESP_ERR_INVALID_STATE;

    eda_visualizer_set_audio_source(EDA_AUDIO_SRC_WIFI);  // 源=PC 推流
    if (s_audio_src != EDA_AUDIO_SRC_WIFI) {
        return ESP_FAIL;  // UDP 接收器没起来
    }
    s_music_mode = true;
    s_auto_follow = false;        // 锁定灯效，不被设备状态覆盖
    s_stream_lost_us = 0;
    if (s_ai_cb) s_ai_cb(false);  // 暂停小智唤醒/识别
    ESP_LOGI(TAG, ">> 进入音乐模式：源=WiFi推流，AI语音已暂停");
    return ESP_OK;
}

void eda_visualizer_exit_music_mode(void) {
    if (!s_music_mode) return;
    s_music_mode = false;
    eda_visualizer_set_audio_source(EDA_AUDIO_SRC_MIC);
    s_auto_follow = true;         // 恢复状态跟随
    s_stream_lost_us = 0;
    if (s_ai_cb) s_ai_cb(true);   // 恢复小智语音
    ESP_LOGI(TAG, ">> 退出音乐模式：源=麦克风，AI语音已恢复");
}

// 推流变化 -> 自动进入/退出音乐模式（在 vis_task 内调用）
// 只在"推流上升沿"自动进入：手动退出后，只要流没断就不再自动进入，
// 否则用户永远退不出音乐模式（流一直推 -> 每帧又自动进）。
static void music_mode_auto_tick(bool streaming) {
    bool rising = streaming && !s_prev_streaming;
    s_prev_streaming = streaming;

    if (!s_music_mode) {
        if (rising) {
            ESP_LOGI(TAG, "检测到推流(上升沿)，自动进入音乐模式");
            eda_visualizer_enter_music_mode();
        }
        return;
    }
    if (streaming) {
        s_stream_lost_us = 0;
        return;
    }
    int64_t now = esp_timer_get_time();
    if (s_stream_lost_us == 0) {
        s_stream_lost_us = now;
    } else if ((now - s_stream_lost_us) > (int64_t)MUSIC_AUTO_EXIT_MS * 1000) {
        ESP_LOGI(TAG, "推流中断超时，自动退出音乐模式");
        eda_visualizer_exit_music_mode();
    }
}

// 事件回调只置标志（禁止在 default event loop 里做 socket/建任务等重活），
// 实际 UDP 启动由 vis_task 下个节拍执行。
static volatile bool s_start_udp_rx = false;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    s_start_udp_rx = true;
}

void eda_visualizer_feed(const int16_t *pcm, int samples) {
    if (!s_started) return;
    audio_processor_feed(pcm, samples);
}

// ---------------- 命令消费 ----------------

static void apply_command(const core_command_t *cmd) {
    switch (cmd->type) {
    case CMD_MODE_CHANGE:
        led_set_mode(cmd->data.mode);
        s_current_mode = cmd->data.mode;
        break;
    case CMD_BRIGHTNESS_SET:
        led_set_brightness((uint8_t)cmd->data.param.value);
        ESP_LOGI(TAG, "亮度设为 %d%%", cmd->data.param.value);
        break;
    case CMD_BRIGHTNESS_UP:
        led_set_brightness((uint8_t)(led_get_brightness() + 10));
        break;
    case CMD_BRIGHTNESS_DOWN:
        led_set_brightness((uint8_t)(led_get_brightness() - 10));
        break;
    case CMD_POST_SET:
        led_set_post_params(cmd->data.post.gamma, cmd->data.post.gate,
                            cmd->data.post.afterimage);
        break;
    case CMD_FX_SET: {
        led_fx_t fx = {
            .speed = cmd->data.fx.speed,
            .intensity = cmd->data.fx.intensity,
            .sensitivity = cmd->data.fx.sensitivity,
            .hue = cmd->data.fx.hue,
            .color_speed = cmd->data.fx.color_speed,
            .beat_react = cmd->data.fx.beat_react,
        };
        led_set_fx(&fx);
        break;
    }
    case CMD_TEST_RAINBOW: {
        core_command_t m = { .type = CMD_MODE_CHANGE };
        m.data.mode = MODE_RAINBOW;
        post_command(&m);
        break;
    }
    case CMD_SET_PARAM:
    case CMD_GET_STATUS:
    default:
        break;
    }
}

// ---------------- 渲染任务 ----------------

static void vis_task(void *arg) {
    int brightness = (int)arg;
    led_set_brightness((uint8_t)brightness);
    led_set_mode(MODE_SPECTRUM);

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        // 1. 消费外部命令（保持单一数据源）
        core_command_t cmd;
        while (dual_core_com_receive_command(&cmd, 0) == ESP_OK) {
            apply_command(&cmd);
        }

        // 1.5 延后的 UDP 推流接收器启动（事件回调置标志，这里安全执行）
        if (s_start_udp_rx) {
            s_start_udp_rx = false;
            if (!s_wifi_rx_up) {
                if (wifi_audio_init(WIFI_AUDIO_UDP_PORT) == ESP_OK) {
                    s_wifi_rx_up = true;
                    ESP_LOGI(TAG, "推流接收器已启动 (UDP %d, 发现 5005)", WIFI_AUDIO_UDP_PORT);
                } else {
                    ESP_LOGW(TAG, "推流接收器启动失败（不影响麦克风源）");
                }
            }
        }

        // 1.6 音乐模式自动进入/退出（依据推流是否在流动）
        music_mode_auto_tick(wifi_audio_streaming());

        // 2. 只有音乐模式才做 FFT（吃 PC 推流）。聊天模式完全不用音频做渲染，
        //    灯光交给情绪场景（也不读麦克风，避免与语音链路争抢）。
        if (s_music_mode) {
            if (s_fft.sample_rate != 16000) s_fft.sample_rate = 16000;
            int processed = 0;
            while (processed < 3 && wifi_audio_available() >= FFT_SIZE) {
                wifi_audio_read(s_wifi_frame, FFT_SIZE, 0);
                if (fft_processor_process_buffer(&s_fft, s_wifi_frame, FFT_SIZE) != ESP_OK) break;
                processed++;
            }
        }

        // 3. 驱动灯效：音乐模式=FFT 频带；聊天模式=静音频带（情绪场景的时间动画）
        led_update_visualization(s_music_mode ? s_fft.frequency_bands : s_silent_bands,
                                 NUM_FREQ_BANDS);

        // 4. 刷新共享状态（供 Web 控制台 / MCP 读取）
        s_frame_count++;
        dual_core_com_update_status(s_current_mode, s_frame_count);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(VIS_FPS_MS));
    }
}

// ---------------- 启动 ----------------

esp_err_t eda_visualizer_start(int gpio, int led_num, int brightness_percent) {
    if (s_started) return ESP_OK;

    ESP_ERROR_CHECK(dual_core_com_init());

    esp_err_t ret = fft_processor_init(&s_fft, 16000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    led_config_t cfg = {
        .gpio_pin = gpio,
        .num_leds = led_num,
        .brightness = 255,
    };
    ret = led_controller_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED 控制器初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_started = true;
    // UDP 接收器不能在此创建：板级构造早于 esp_netif/lwIP 初始化，socket() 会 assert。
    // 挂 IP 事件后再启动（此时网络栈就绪），见 on_got_ip。
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL);
    BaseType_t ok = xTaskCreatePinnedToCore(vis_task, "eda_vis", 8192,
                                            (void *)(intptr_t)brightness_percent,
                                            2, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "渲染任务创建失败");
        s_started = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "氛围灯引擎已启动: GPIO=%d LEDs=%d (T2: 音量律动待接 emotion)",
             gpio, led_num);
    return ESP_OK;
}
