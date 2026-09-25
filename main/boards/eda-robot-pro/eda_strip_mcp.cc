/*
 * eda_strip_mcp —— 氛围灯 MCP 工具注册（语音控制入口）
 *
 * 注册后小智云端 AI 才能在工具列表里"看懂"灯效控制意图，
 * 例如"把氛围灯调成烟花模式""亮度调到30%""关灯"。
 */
#include <cstring>
#include <string>
#include <vector>

#include <esp_log.h>

#include "eda_visualizer.h"
#include "led_controller.h"
#include "mcp_server.h"

#define TAG "EdaStripMcp"

struct ModeEntry {
    const char* name;
    led_mode_t mode;
    const char* desc;
};

static const ModeEntry kModes[] = {
    {"spectrum",       MODE_SPECTRUM,       "频谱律动（随音乐跳动）"},
    {"rainbow",        MODE_RAINBOW,        "彩虹流转"},
    {"starfield",      MODE_STARFIELD,      "星空漂移"},
    {"meteor_pulse",   MODE_METEOR_PULSE,   "流星脉冲"},
    {"water_ripple",   MODE_WATER_RIPPLE,   "水波纹"},
    {"energy_wave",    MODE_ENERGY_WAVE,    "能量波"},
    {"fireworks",      MODE_FIREWORKS,      "烟花"},
    {"rhythm_pulse",   MODE_RHYTHM_PULSE,   "节奏脉冲"},
    {"rhythm_breath",  MODE_RHYTHM_BREATH,  "节奏呼吸"},
    {"rhythm_jump",    MODE_RHYTHM_JUMP,    "节奏跳动"},
    {"sparkle_rainbow",MODE_SPARKLE_RAINBOW,"闪烁彩虹"},
    {"explosion",      MODE_EXPLOSION,      "爆炸碰撞"},
    {"peak_hold",      MODE_PEAK_HOLD,      "波峰余晖"},
    {"mirror",         MODE_MIRROR,         "对称频谱"},
    {"shockwave",      MODE_SHOCKWAVE,      "节拍冲击波"},
    {"aurora",         MODE_AURORA,         "极光辉光"},
    {"heartbeat",      MODE_HEARTBEAT,      "心跳闪动"},
    {"off",            MODE_OFF,            "关灯"},
};

static const char* mode_to_name(led_mode_t m) {
    for (const auto& e : kModes) {
        if (e.mode == m) return e.name;
    }
    return "unknown";
}

void InitializeEdaStripController() {
    auto& mcp = McpServer::GetInstance();

    std::string mode_doc = "切换AI氛围灯的灯效模式。mode 取值如下：";
    for (const auto& e : kModes) {
        mode_doc += std::string("'") + e.name + "'=" + e.desc + "；";
    }

    mcp.AddTool(
        "self.strip.set_mode",
        mode_doc.c_str(),
        PropertyList({Property("mode", kPropertyTypeString, "spectrum")}),
        [](const PropertyList& properties) -> ReturnValue {
            std::string name = properties["mode"].value<std::string>();
            for (const auto& e : kModes) {
                if (name == e.name) {
                    eda_visualizer_set_mode(e.mode);
                    ESP_LOGI(TAG, "语音切换灯效: %s", e.name);
                    return std::string("氛围灯已切换到") + e.desc;
                }
            }
            return "无效的灯效模式名";
        });

    mcp.AddTool(
        "self.strip.set_brightness",
        "设置氛围灯亮度。brightness: 亮度百分比(0-100)，0 为最暗",
        PropertyList({Property("brightness", kPropertyTypeInteger, 60, 0, 100)}),
        [](const PropertyList& properties) -> ReturnValue {
            int v = properties["brightness"].value<int>();
            eda_visualizer_set_brightness(v);
            ESP_LOGI(TAG, "语音设置亮度: %d%%", v);
            return true;
        });

    mcp.AddTool(
        "self.strip.next_mode",
        "将氛围灯切换到下一种灯效模式（用户说'换个灯效/切换模式'但没有指定具体模式时使用）",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            led_mode_t current = eda_visualizer_get_mode();
            int idx = -1;
            for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); i++) {
                if (kModes[i].mode == current) { idx = (int)i; break; }
            }
            int next = (idx + 1) % (int)(sizeof(kModes) / sizeof(kModes[0]));
            eda_visualizer_set_mode(kModes[next].mode);
            ESP_LOGI(TAG, "语音切换下一模式: %s", kModes[next].name);
            return std::string("氛围灯已切换到") + kModes[next].desc;
        });

    mcp.AddTool(
        "self.strip.set_auto_follow",
        "控制氛围灯是否自动跟随设备状态切换灯效。follow=true 表示恢复自动（待机频谱/聆听能量波/说话节奏脉冲等自动切换）；"
        "follow=false 表示锁定当前灯效。用户说'恢复灯光自动模式/让灯跟着状态变'时传true，"
        "说'锁定灯效/别让状态影响灯'时传false",
        PropertyList({Property("follow", kPropertyTypeBoolean, true)}),
        [](const PropertyList& properties) -> ReturnValue {
            bool follow = properties["follow"].value<bool>();
            eda_visualizer_set_auto_follow(follow);
            ESP_LOGI(TAG, "语音设置自动跟随: %d", (int)follow);
            return follow ? std::string("氛围灯已恢复自动跟随设备状态")
                          : std::string("氛围灯已锁定当前灯效");
        });

    mcp.AddTool(
        "self.strip.set_audio_source",
        "切换氛围灯跟随的音频来源。source='wifi' 表示使用电脑通过UDP推流过来的音乐（用户说'用电脑的音乐'/'推流模式'/'播放电脑声音'时）；"
        "source='mic' 表示使用设备麦克风听周围环境声（用户说'用麦克风'/'切回周围声音'时）。推流断开时会自动临时回落麦克风",
        PropertyList({Property("source", kPropertyTypeString, "mic")}),
        [](const PropertyList& properties) -> ReturnValue {
            std::string src = properties["source"].value<std::string>();
            if (src == "wifi") {
                eda_visualizer_set_audio_source(EDA_AUDIO_SRC_WIFI);
                return std::string("氛围灯已切换到电脑推流源，请在电脑上运行推流脚本");
            }
            eda_visualizer_set_audio_source(EDA_AUDIO_SRC_MIC);
            return std::string("氛围灯已切回麦克风环境声源");
        });

    mcp.AddTool(
        "self.strip.get_status",
        "查询氛围灯当前状态（当前模式、亮度、是否自动跟随、音频来源、是否正在接收推流、检测到的音乐节拍BPM）",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "mode", mode_to_name(eda_visualizer_get_mode()));
            cJSON_AddNumberToObject(root, "brightness", eda_visualizer_get_brightness());
            cJSON_AddBoolToObject(root, "auto_follow", eda_visualizer_is_auto_follow());
            cJSON_AddStringToObject(root, "audio_source",
                eda_visualizer_get_audio_source() == EDA_AUDIO_SRC_WIFI ? "wifi" : "mic");
            cJSON_AddBoolToObject(root, "streaming", eda_visualizer_audio_streaming());
            cJSON_AddNumberToObject(root, "bpm", eda_visualizer_get_bpm());
            return root;
        });

    ESP_LOGI(TAG, "氛围灯 MCP 工具注册完成");
}
