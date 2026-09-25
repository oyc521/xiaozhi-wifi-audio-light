#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_network.h>
#include <esp_log.h>

#include <font_awesome.h>
#include <wifi_manager.h>
#include <ssid_manager.h>
#include "afsk_demod.h"

static const char *TAG = "WifiBoard";

#define WIFI_BOARD_CONNECTED_BIT BIT0

WifiBoard::WifiBoard() {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterWifiConfigMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);
    wifi_config_mode_ = true;

    auto& wifi_manager = WifiManager::GetInstance();

    // 等待用户在配网页面提交新凭据（v3 协议：保存并验证成功后 portal 请求 exit）
    EventGroupHandle_t events = xEventGroupCreate();
    wifi_manager.SetEventCallback([events](WifiEvent event, const std::string& data) {
        if (event == WifiEvent::ConfigModeExit) {
            xEventGroupSetBits(events, BIT1);
        }
    });

    wifi_manager.StartConfigAp();

    // 等待 1.5 秒显示开发板信息
    vTaskDelay(pdMS_TO_TICKS(1500));

    // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_manager.GetApSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_manager.GetApWebUrl();
    hint += "\n\n";

    // 播报配置 WiFi 的提示
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_WIFICONFIG);

    #if CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING
    auto display = Board::GetInstance().GetDisplay();
    auto codec = Board::GetInstance().GetAudioCodec();
    int channel = 1;
    if (codec) {
        channel = codec->input_channels();
    }
    ESP_LOGI(TAG, "Start receiving WiFi credentials from audio, input channels: %d", channel);
    audio_wifi_config::ReceiveWifiCredentialsFromAudio(&application, &wifi_ap, display, channel);
    #endif

    // Block until the user submits credentials from the portal, then return
    // so StartNetwork can retry the station with the new config.
    xEventGroupWaitBits(events, BIT1, pdFALSE, pdFALSE, portMAX_DELAY);
    wifi_manager.SetEventCallback(nullptr);
    vEventGroupDelete(events);
    wifi_config_mode_ = false;
}

bool WifiBoard::TryConnectStation() {
    auto& wifi_manager = WifiManager::GetInstance();

    auto ssid_list = SsidManager::GetInstance().GetSsidList();
    if (ssid_list.empty()) {
        return false;
    }

    EventGroupHandle_t events = xEventGroupCreate();
    wifi_manager.SetEventCallback([events](WifiEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        switch (event) {
        case WifiEvent::Scanning:
            display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
            break;
        case WifiEvent::Connecting: {
            std::string notification = Lang::Strings::CONNECT_TO;
            notification += data;
            notification += "...";
            display->ShowNotification(notification.c_str(), 30000);
            break;
        }
        case WifiEvent::Connected: {
            std::string notification = Lang::Strings::CONNECTED_TO;
            notification += data;
            display->ShowNotification(notification.c_str(), 30000);
            xEventGroupSetBits(events, WIFI_BOARD_CONNECTED_BIT);
            break;
        }
        default:
            break;
        }
    });

    wifi_manager.StartStation();
    EventBits_t bits = xEventGroupWaitBits(events, WIFI_BOARD_CONNECTED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(60 * 1000));
    // 顺序关键：先摘回调（不再有事件写入），再删事件组，最后停 station。
    // 若先删事件组，迟到的事件会写已释放内存 → 崩溃重启循环。
    wifi_manager.SetEventCallback(nullptr);
    vEventGroupDelete(events);

    if (bits & WIFI_BOARD_CONNECTED_BIT) {
        return true;
    }

    ESP_LOGW(TAG, "WiFi connect timeout, stopping station");
    wifi_manager.StopStation();
    return false;
}

void WifiBoard::StartNetwork() {
    auto& wifi_manager = WifiManager::GetInstance();

    WifiManagerConfig config;
    config.ssid_prefix = "Xiaozhi";
    config.language = Lang::CODE;
    wifi_manager.Initialize(config);

    // User can press BOOT button while starting to enter WiFi configuration mode
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
    }

    // Connect; on failure loop through config mode until we have a link
    while (!TryConnectStation()) {
        EnterWifiConfigMode();
    }
}

NetworkInterface* WifiBoard::GetNetwork() {
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon() {
    auto& wifi = WifiManager::GetInstance();
    if (wifi_config_mode_ || wifi.IsConfigMode()) {
        return FONT_AWESOME_WIFI;
    }
    if (!wifi.IsConnected()) {
        return FONT_AWESOME_WIFI_SLASH;
    }
    int rssi = wifi.GetRssi();
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    }
    return FONT_AWESOME_WIFI_WEAK;
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    auto& wifi = WifiManager::GetInstance();
    std::string board_json = R"({)";
    board_json += R"("type":")" + std::string(BOARD_TYPE) + R"(",)";
    board_json += R"("name":")" + std::string(BOARD_NAME) + R"(",)";
    if (!wifi_config_mode_) {
        board_json += R"("ssid":")" + wifi.GetSsid() + R"(",)";
        board_json += R"("rssi":)" + std::to_string(wifi.GetRssi()) + R"(,)";
        board_json += R"("channel":)" + std::to_string(wifi.GetChannel()) + R"(,)";
        board_json += R"("ip":")" + wifi.GetIpAddress() + R"(",)";
    }
    board_json += R"("mac":")" + SystemInfo::GetMacAddress() + R"(")";
    board_json += R"(})";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    WifiManager::GetInstance().SetPowerSaveLevel(
        enabled ? WifiPowerSaveLevel::LOW_POWER : WifiPowerSaveLevel::PERFORMANCE);
}

void WifiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to enter the network configuration mode
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Reboot the device
    esp_restart();
}

std::string WifiBoard::GetDeviceStatusJson() {
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    // Audio speaker
    auto audio_speaker = cJSON_CreateObject();
    auto audio_codec = board.GetAudioCodec();
    if (audio_codec) {
        cJSON_AddNumberToObject(audio_speaker, "volume", audio_codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    // Screen brightness
    auto backlight = board.GetBacklight();
    auto screen = cJSON_CreateObject();
    if (backlight) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    auto display = board.GetDisplay();
    if (display && display->height() > 64) { // For LCD display only
        auto theme = display->GetTheme();
        if (theme != nullptr) {
            cJSON_AddStringToObject(screen, "theme", theme->name().c_str());
        }
    }
    cJSON_AddItemToObject(root, "screen", screen);

    // Battery
    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        cJSON* battery = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery, "level", battery_level);
        cJSON_AddBoolToObject(battery, "charging", charging);
        cJSON_AddItemToObject(root, "battery", battery);
    }

    // Network
    auto& wifi = WifiManager::GetInstance();
    auto network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi.GetSsid().c_str());
    int rssi = wifi.GetRssi();
    if (rssi >= -60) {
        cJSON_AddStringToObject(network, "signal", "strong");
    } else if (rssi >= -70) {
        cJSON_AddStringToObject(network, "signal", "medium");
    } else {
        cJSON_AddStringToObject(network, "signal", "weak");
    }
    cJSON_AddItemToObject(root, "network", network);

    // Chip
    float esp32temp = 0.0f;
    if (board.GetTemperature(esp32temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", esp32temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return json;
}
