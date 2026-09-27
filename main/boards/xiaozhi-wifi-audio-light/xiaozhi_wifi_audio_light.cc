#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/led.h"
#if CONFIG_OYC_AMBIENT_LIGHT
#include "oyc_visualizer.h"
#else
#include "led/circular_strip.h"
#endif

#include <wifi_station.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <font_awesome.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <oyc_web_console.h>
#include <oyc_lan_ota.h>

#define TAG "XiaozhiWifiAudioLight"

#if CONFIG_OYC_AMBIENT_LIGHT
extern void InitializeOycStripController();
#endif

#if CONFIG_OYC_AMBIENT_LIGHT
// 麦克风 PCM 抽头：不新增 I2S，直接转发给氛围灯 FFT 引擎
class OycAudioCodecSimplex : public NoAudioCodecSimplex {
public:
    using NoAudioCodecSimplex::NoAudioCodecSimplex;
    virtual bool InputData(std::vector<int16_t>& data) override {
        bool ok = NoAudioCodecSimplex::InputData(data);
        if (ok && !data.empty()) {
            oyc_visualizer_feed(data.data(), (int)data.size());
        }
        return ok;
    }
};

// 设备状态 -> 灯效场景（application.cc 在状态变化时调用 OnStateChanged）
// 走 set_mode_auto：用户语音指定模式（锁定）后，这些切换不再覆盖
class OycAmbientLed : public Led {
public:
    virtual void OnStateChanged() override {
        auto state = Application::GetInstance().GetDeviceState();
        switch (state) {
        case kDeviceStateWifiConfiguring:
        case kDeviceStateConnecting:
            oyc_visualizer_set_mode_auto(MODE_AURORA);        // 联网中 = 极光
            break;
        case kDeviceStateUpgrading:
            oyc_visualizer_set_mode_auto(MODE_RHYTHM_BREATH); // 升级中 = 呼吸
            break;
        default:
            break;   // 其余状态由情绪灯（SetEmotion）驱动
        }
        // 屏幕：待机/聆听/说话 = 64 大脸；其它系统态 = 状态栏 + 48 小脸
        auto* oled = static_cast<OledDisplay*>(Board::GetInstance().GetDisplay());
        if (oled != nullptr) {
            bool big = (state == kDeviceStateIdle ||
                        state == kDeviceStateListening ||
                        state == kDeviceStateSpeaking);
            oled->EnableBigFace(big);
        }
    }
};

// 音乐模式互斥（保留唤醒词版）：
//   进入音乐模式只暂停 ASR（语音识别），保留唤醒词运行，
//   这样放音乐时仍可说"你好小智"打断，退出音乐模式进入对话。
static void OycAiAudioToggle(bool enable) {
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();
    if (enable) {
        // 退出音乐模式：空闲时才需恢复唤醒词；ASR 由状态机在进入 listening 时开启
        if (app.GetDeviceState() == kDeviceStateIdle) {
            audio.EnableWakeWordDetection(true);
        }
        ESP_LOGI(TAG, "AI 语音已恢复");
    } else {
        // 进入灯光模式：只在空闲时确保唤醒词开启。若正在对话(listening/speaking)，
        // 绝不能打开唤醒词，否则唤醒词与 ASR 同时运行，audio_input 只会喂唤醒词，
        // 既听不到你说话，又会让唤醒词 AFE 的 FEED 环溢出。
        if (app.GetDeviceState() == kDeviceStateIdle) {
            if (audio.IsAudioProcessorRunning()) audio.EnableVoiceProcessing(false);
            audio.EnableWakeWordDetection(true);
        }
        ESP_LOGI(TAG, "灯光模式：仅空闲时保留唤醒词（对话中不动 AI）");
    }
}

// 周期守卫（音乐 / 氛围）：
//  音乐模式：唤醒词被触发 -> 退出音乐模式交还对话；空闲时关 ASR，推流音源保留唤醒词、
//            环境麦音源关闭唤醒词（避免与 OycAmbientMicTask 抢麦）。
//  氛围模式：不退出、保留唤醒词、允许语音切灯效；对话期间不动 ASR。
static esp_timer_handle_t s_music_guard_timer = nullptr;
static DeviceState s_guard_prev_state = kDeviceStateUnknown;
static void OycModeGuard(void *arg) {
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();
    auto state = app.GetDeviceState();

    if (oyc_visualizer_is_music_mode()) {
        // 只有“空闲被唤醒”（上一拍 idle -> 这一拍进入对话）才算唤醒词打断音乐模式。
        // 用语音“进入音乐模式”时，state 本来就在 listening/speaking，不应立即退出。
        bool fresh_wake = (s_guard_prev_state == kDeviceStateIdle) &&
                          (state == kDeviceStateConnecting ||
                           state == kDeviceStateListening ||
                           state == kDeviceStateSpeaking);
        s_guard_prev_state = state;
        if (fresh_wake) {
            ESP_LOGI(TAG, "音乐模式检测到语音唤醒，退出音乐模式进入对话");
            oyc_visualizer_exit_music_mode();
            return;
        }
        if (audio.IsAudioProcessorRunning()) audio.EnableVoiceProcessing(false);

        bool wifi_src = (oyc_visualizer_get_audio_source() == OYC_AUDIO_SRC_WIFI);
        if (wifi_src) {
            if (!audio.IsWakeWordRunning()) audio.EnableWakeWordDetection(true);
            // 持续关闭 WiFi 省电：对话结束回到 idle 时应用会把它设回 MAX_MODEM
            wifi_ps_type_t ps;
            if (esp_wifi_get_ps(&ps) == ESP_OK && ps != WIFI_PS_NONE) {
                esp_wifi_set_ps(WIFI_PS_NONE);
            }
        } else {
            if (audio.IsWakeWordRunning()) audio.EnableWakeWordDetection(false);
        }
        return;
    }

    s_guard_prev_state = state;

    if (oyc_visualizer_is_ambient_mode()) {
        // 氛围模式：稳定渲染，保留唤醒词用于语音控制；不做模式切换。
        // 仅在空闲时确保唤醒词在跑；对话(listening/speaking)期间交给状态机管理，
        // 否则会把对话中本应关闭的唤醒词又打开，导致重复唤醒。
        if (state == kDeviceStateIdle) {
            if (!audio.IsWakeWordRunning()) audio.EnableWakeWordDetection(true);
        }
        return;
    }
}

// 情绪钩子：屏幕 SetEmotion 时同步驱动氛围灯（聊天模式的情绪灯）
// 音乐/氛围模式下则显示对应图标，不显示 AI 表情。
class XiaozhiOledDisplay : public OledDisplay {
public:
    using OledDisplay::OledDisplay;
    void SetEmotion(const char* emotion) override {
        oyc_visual_mode_t mode = oyc_visualizer_get_visual_mode();
        if (mode == OYC_VIS_MUSIC) {
            SetModeIcon(FONT_AWESOME_MUSIC);
        } else if (mode == OYC_VIS_AMBIENT) {
            SetModeIcon(FONT_AWESOME_BRIGHTNESS);
        } else {
            ClearModeIcon();
            OledDisplay::SetEmotion(emotion);
        }
        oyc_visualizer_set_emotion(emotion);
    }
};

// 模式切换 -> 屏幕图标（由 oyc_visualizer 回调，可能来自任意任务，内部已加 LVGL 锁）
static OledDisplay* s_oled = nullptr;
static oyc_visual_mode_t s_vis_prev = OYC_VIS_CHAT;   // 与上电初始一致，首帧回调不触发 WiFi 操作
static void OycVisualModeChanged(oyc_visual_mode_t mode) {
    // 音乐模式在推流：关闭 WiFi 省电，避免 modem sleep 让 UDP 成批到达导致频谱卡顿。
    // 仅在“进入/退出音乐模式”的跳变时操作，避免启动早期(WiFi 未就绪)调用；
    // 直接 esp_wifi_set_ps（返回错误不 abort），不要用会 ESP_ERROR_CHECK 的封装。
    if (mode != s_vis_prev) {
        if (mode == OYC_VIS_MUSIC) {
            esp_wifi_set_ps(WIFI_PS_NONE);
        } else if (s_vis_prev == OYC_VIS_MUSIC) {
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        }
        s_vis_prev = mode;
    }
    if (s_oled == nullptr) return;
    if (mode == OYC_VIS_MUSIC) {
        s_oled->SetModeIcon(FONT_AWESOME_MUSIC);
    } else if (mode == OYC_VIS_AMBIENT) {
        s_oled->SetModeIcon(FONT_AWESOME_BRIGHTNESS);
    } else {
        s_oled->ClearModeIcon();
        s_oled->SetEmotion("neutral");
    }
}

// 环境声音源：音乐模式下 AI 已暂停、AFE 不再读麦，由我们自读 codec 喂给可视化。
// （此时没有其它读者，不会与小智争抢同一路 I2S）
static void OycAmbientMicTask(void *arg) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) { vTaskDeleteWithCaps(NULL); return; }
    std::vector<int16_t> buf(512);
    while (true) {
        if (oyc_visualizer_wants_ambient_mic()) {
            if (!codec->input_enabled()) codec->EnableInput(true);
            if (codec->InputData(buf) && !buf.empty()) {
                oyc_visualizer_feed(buf.data(), (int)buf.size());
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
}
#endif

class XiaozhiWifiAudioLight : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
#if CONFIG_OYC_AMBIENT_LIGHT
    Led* ambient_led_ = nullptr;
#else
    CircularStrip* strip_ = nullptr;
#endif
    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

#if CONFIG_OYC_AMBIENT_LIGHT
        display_ = new XiaozhiOledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#else
        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#endif
    }


    void InitializeButtons() {

        touch_button_.OnPressDown([this]() {
#if CONFIG_OYC_AMBIENT_LIGHT
            if (oyc_visualizer_is_music_mode()) {
                oyc_visualizer_exit_music_mode();   // TOUCH 键 = 退出音乐模式
                return;
            }
#endif
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });


    }

public:
    XiaozhiWifiAudioLight() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO){
        // 启动即确认当前镜像有效，避免"未确认 → 重启被 bootloader 回滚成旧固件"
        // （旧逻辑确认点在联网后的 CheckNewVersion，无网时永远确认不了）
        oyc_lan_ota_confirm_image();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
#if CONFIG_OYC_AMBIENT_LIGHT
        ambient_led_ = new OycAmbientLed();
        // 程序化 Q 版脸 + 模式图标
        s_oled = static_cast<OledDisplay*>(display_);
        if (s_oled) {
            s_oled->EnableCuteFace(true);   // 大/小脸由设备状态在 OnStateChanged 里切换
            oyc_visualizer_set_visual_mode_cb(OycVisualModeChanged);
        }
        oyc_visualizer_start(STRIP_GPIO, STRIP_LED_NUM, CONFIG_OYC_STRIP_BRIGHTNESS);
        oyc_visualizer_set_ai_audio_cb(OycAiAudioToggle);
        // 环境声子模式：需要时自读麦克风喂可视化（栈放 PSRAM）
        xTaskCreatePinnedToCoreWithCaps(OycAmbientMicTask, "oyc_amic", 4096, nullptr, 3, nullptr, 1, MALLOC_CAP_SPIRAM);
        // 音乐模式期间每秒重申一次"暂停 AI"，防止小智回 idle 时自恢复唤醒
        esp_timer_create_args_t guard_args = { .callback = OycModeGuard, .name = "mode_guard" };
        esp_timer_create(&guard_args, &s_music_guard_timer);
        esp_timer_start_periodic(s_music_guard_timer, 200 * 1000);   // 200ms：及时响应唤醒词打断
        InitializeOycStripController();
#else
        strip_ = new CircularStrip(STRIP_GPIO, STRIP_LED_NUM);
#endif
        oyc_web_console_init();
    }


    virtual Led* GetLed() override {
#if CONFIG_OYC_AMBIENT_LIGHT
        return ambient_led_;
#else
        return strip_;
#endif
    }



    virtual AudioCodec* GetAudioCodec() override {
#if CONFIG_OYC_AMBIENT_LIGHT
        static OycAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(XiaozhiWifiAudioLight);
