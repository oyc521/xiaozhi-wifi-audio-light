#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/led.h"
#if CONFIG_EDA_AMBIENT_LIGHT
#include "eda_visualizer.h"
#else
#include "led/circular_strip.h"
#endif

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <eda_web_console.h>
#include <eda_lan_ota.h>

#define TAG "EDARobotPro"

extern void InitializeEDARobotDogController();
#if CONFIG_EDA_AMBIENT_LIGHT
extern void InitializeEdaStripController();
#endif

#if CONFIG_EDA_AMBIENT_LIGHT
// 麦克风 PCM 抽头：不新增 I2S，直接转发给氛围灯 FFT 引擎
class EdaAudioCodecSimplex : public NoAudioCodecSimplex {
public:
    using NoAudioCodecSimplex::NoAudioCodecSimplex;
    virtual bool InputData(std::vector<int16_t>& data) override {
        bool ok = NoAudioCodecSimplex::InputData(data);
        if (ok && !data.empty()) {
            eda_visualizer_feed(data.data(), (int)data.size());
        }
        return ok;
    }
};

// 设备状态 -> 灯效场景（application.cc 在状态变化时调用 OnStateChanged）
// 走 set_mode_auto：用户语音指定模式（锁定）后，这些切换不再覆盖
class EdaAmbientLed : public Led {
public:
    virtual void OnStateChanged() override {
        switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateIdle:
            eda_visualizer_set_mode_auto(MODE_SPECTRUM);      // 待机 = 音乐频谱律动
            break;
        case kDeviceStateListening:
            eda_visualizer_set_mode_auto(MODE_ENERGY_WAVE);   // 聆听 = 能量波
            break;
        case kDeviceStateSpeaking:
            eda_visualizer_set_mode_auto(MODE_RHYTHM_PULSE);  // 说话 = 节奏脉冲
            break;
        case kDeviceStateWifiConfiguring:
        case kDeviceStateConnecting:
            eda_visualizer_set_mode_auto(MODE_AURORA);        // 联网中 = 极光
            break;
        case kDeviceStateUpgrading:
            eda_visualizer_set_mode_auto(MODE_RHYTHM_BREATH); // 升级中 = 呼吸
            break;
        default:
            break;
        }
    }
};

// 音乐模式互斥：暂停/恢复小智的唤醒词与语音识别
static void EdaAiAudioToggle(bool enable) {
    auto& audio = Application::GetInstance().GetAudioService();
    if (enable) {
        audio.EnableWakeWordDetection(true);
        ESP_LOGI(TAG, "AI 语音已恢复");
    } else {
        audio.EnableVoiceProcessing(false);
        audio.EnableWakeWordDetection(false);
        ESP_LOGI(TAG, "AI 语音已暂停（音乐模式）");
    }
}
#endif

class EDARobotPro : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
#if CONFIG_EDA_AMBIENT_LIGHT
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

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }


    // EDA机器狗控制器初始化
    void InitializeEDARobotDogController() {
        ESP_LOGI(TAG, "初始化EDA机器狗MCP控制器");
        ::InitializeEDARobotDogController();
    }
    void InitializeButtons() {

        touch_button_.OnPressDown([this]() {
#if CONFIG_EDA_AMBIENT_LIGHT
            if (eda_visualizer_is_music_mode()) {
                eda_visualizer_exit_music_mode();   // TOUCH 键 = 退出音乐模式
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
    EDARobotPro() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO){
        // 启动即确认当前镜像有效，避免"未确认 → 重启被 bootloader 回滚成旧固件"
        // （旧逻辑确认点在联网后的 CheckNewVersion，无网时永远确认不了）
        eda_lan_ota_confirm_image();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeEDARobotDogController();
        InitializeButtons();
#if CONFIG_EDA_AMBIENT_LIGHT
        ambient_led_ = new EdaAmbientLed();
        eda_visualizer_start(STRIP_GPIO, STRIP_LED_NUM, CONFIG_EDA_STRIP_BRIGHTNESS);
        eda_visualizer_set_ai_audio_cb(EdaAiAudioToggle);
        InitializeEdaStripController();
#else
        strip_ = new CircularStrip(STRIP_GPIO, STRIP_LED_NUM);
#endif
        eda_web_console_init();
    }


    virtual Led* GetLed() override {
#if CONFIG_EDA_AMBIENT_LIGHT
        return ambient_led_;
#else
        return strip_;
#endif
    }



    virtual AudioCodec* GetAudioCodec() override {
#if CONFIG_EDA_AMBIENT_LIGHT
        static EdaAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
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

DECLARE_BOARD(EDARobotPro);
