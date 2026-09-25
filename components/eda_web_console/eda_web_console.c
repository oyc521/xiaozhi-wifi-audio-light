/*
 * eda_web_console —— 单实例 80 端口 Web 控制台（开发用途）
 *
 * 拥有唯一的 esp_http_server，注册：控制台页面 / API / 一键推流脚本 / OTA。
 * 仅在 STA 拿到 IP 后启动、断开即停，因此 AP 配网门户(也占80)永不冲突。
 *
 * 后端契约与 FFTvisiual1.0ws2812b 的 wifi_core 对齐，直接复用其 console.html，
 * 所有参数修改统一走 eda_visualizer(入队)，遵守单一数据源纪律。
 *
 * 受 CONFIG_EDA_DEV_MODE 控制（量产关闭）。
 */
#include "eda_web_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "dual_core_com.h"
#include "eda_lan_ota.h"
#include "eda_visualizer.h"
#include "led_controller.h"
#include "wifi_audio.h"

#include "sdkconfig.h"

#if CONFIG_EDA_DEV_MODE

#define TAG "EDA_WEB"
#define CONSOLE_PORT 80
#define SPEC_BANDS   32

extern const char console_html_start[] asm("_binary_console_html_start");
extern const char console_html_end[]   asm("_binary_console_html_end");
extern const char loopback_py_start[]  asm("_binary_loopback_py_start");
extern const char loopback_py_end[]    asm("_binary_loopback_py_end");

static httpd_handle_t s_server = NULL;

static void add_cors(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

static bool sta_ip(char *out, int out_len) {
    esp_netif_t *st = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ii;
    if (st && esp_netif_get_ip_info(st, &ii) == ESP_OK && ii.ip.addr != 0) {
        snprintf(out, out_len, IPSTR, IP2STR(&ii.ip));
        return true;
    }
    return false;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    add_cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s ? s : "{}");
    free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

static float qs_float(httpd_req_t *req, const char *key, float def) {
    char q[96] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return def;
    char val[16] = {0};
    if (httpd_query_key_value(q, key, val, sizeof(val)) != ESP_OK) return def;
    return strtof(val, NULL);
}

// ---------------- / 控制台页面 ----------------
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    size_t len = (size_t)(console_html_end - console_html_start);
    return httpd_resp_send(req, console_html_start, len);
}

static esp_err_t ping_handler(httpd_req_t *req) {
    add_cors(req);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "pong", 4);
}

// ---------------- /api/status ----------------
static esp_err_t api_status_handler(httpd_req_t *req) {
    core_status_t st;
    dual_core_com_get_status(&st);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "mode", st.current_mode);
    cJSON_AddBoolToObject(root, "wifi_connected", true);   // server 只在拿到 IP 后才运行
    cJSON_AddNumberToObject(root, "frame_count", st.led_frame_count);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "brightness", eda_visualizer_get_brightness());
    cJSON_AddNumberToObject(root, "bpm", eda_visualizer_get_bpm());
    cJSON_AddNumberToObject(root, "pulse", st.pulse);
    cJSON_AddNumberToObject(root, "energy", st.energy);
    cJSON_AddNumberToObject(root, "gamma", st.gamma);
    cJSON_AddNumberToObject(root, "gate", st.gate);
    cJSON_AddNumberToObject(root, "afterimage", st.afterimage);
    cJSON *fx = cJSON_AddObjectToObject(root, "fx");
    if (fx) {
        cJSON_AddNumberToObject(fx, "speed", st.fx.speed);
        cJSON_AddNumberToObject(fx, "intensity", st.fx.intensity);
        cJSON_AddNumberToObject(fx, "sensitivity", st.fx.sensitivity);
        cJSON_AddNumberToObject(fx, "hue", st.fx.hue);
        cJSON_AddNumberToObject(fx, "color_speed", st.fx.color_speed);
        cJSON_AddNumberToObject(fx, "beat_react", st.fx.beat_react);
    }
    cJSON_AddStringToObject(root, "device_name", "Xiaozhi-WiFi-Audio-Light");
    bool auto_follow = eda_visualizer_is_auto_follow();
    eda_audio_src_t src = eda_visualizer_get_audio_source();
    cJSON_AddBoolToObject(root, "auto_follow", auto_follow);
    cJSON_AddStringToObject(root, "source", src == EDA_AUDIO_SRC_WIFI ? "wifi" : "mic");
    cJSON_AddBoolToObject(root, "wifi_streaming", wifi_audio_streaming());
    cJSON_AddBoolToObject(root, "music_mode", eda_visualizer_is_music_mode());

    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        cJSON_AddStringToObject(root, "fw_version", app->version);
        cJSON_AddStringToObject(root, "project_name", app->project_name);
        cJSON_AddStringToObject(root, "idf_version", app->idf_ver);
    }
    char ip[16] = {0};
    if (sta_ip(ip, sizeof(ip))) cJSON_AddStringToObject(root, "ip_address", ip);

    return send_json(req, root);
}

// ---------------- /api/mode ----------------
static esp_err_t api_mode_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        int m = (int)qs_float(req, "mode", -1);
        if (m < MODE_SPECTRUM || m >= MODE_COUNT) {
            cJSON *r = cJSON_CreateObject();
            cJSON_AddBoolToObject(r, "success", false);
            cJSON_AddStringToObject(r, "error", "mode out of range");
            return send_json(req, r);
        }
        eda_visualizer_set_mode((led_mode_t)m);   // 显式操作，自动锁定跟随
        core_status_t st; dual_core_com_get_status(&st);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "success", true);
        cJSON_AddNumberToObject(r, "mode", m);
        cJSON_AddNumberToObject(r, "current_mode", st.current_mode);
        return send_json(req, r);
    }
    core_status_t st; dual_core_com_get_status(&st);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "mode", st.current_mode);
    cJSON_AddStringToObject(r, "device_name", "Xiaozhi-WiFi-Audio-Light");
    return send_json(req, r);
}

// ---------------- /api/brightness ----------------
static esp_err_t api_brightness_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        int v = (int)qs_float(req, "value", -1);
        if (v < 0 || v > 100) {
            cJSON *r = cJSON_CreateObject();
            cJSON_AddBoolToObject(r, "success", false);
            cJSON_AddStringToObject(r, "error", "out of range 0-100");
            return send_json(req, r);
        }
        eda_visualizer_set_brightness(v);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "success", true);
        cJSON_AddNumberToObject(r, "brightness", v);
        return send_json(req, r);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "brightness", eda_visualizer_get_brightness());
    return send_json(req, r);
}

// ---------------- /api/command ----------------
static esp_err_t api_command_handler(httpd_req_t *req) {
    char q[48] = {0};
    bool ok = false;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char cmd[24] = {0};
        if (httpd_query_key_value(q, "cmd", cmd, sizeof(cmd)) == ESP_OK) {
            if (!strcmp(cmd, "brightness_up")) {
                eda_visualizer_set_brightness(eda_visualizer_get_brightness() + 10); ok = true;
            } else if (!strcmp(cmd, "brightness_down")) {
                eda_visualizer_set_brightness(eda_visualizer_get_brightness() - 10); ok = true;
            } else if (!strcmp(cmd, "test_rainbow")) {
                eda_visualizer_set_mode(MODE_RAINBOW); ok = true;
            } else if (!strcmp(cmd, "clear_all")) {
                eda_visualizer_set_mode(MODE_OFF); ok = true;
            } else if (!strcmp(cmd, "auto_on")) {
                eda_visualizer_set_auto_follow(true); ok = true;
            } else if (!strcmp(cmd, "auto_off")) {
                eda_visualizer_set_auto_follow(false); ok = true;
            }
        }
        (void)ok;
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "success", ok);
    return send_json(req, r);
}

// ---------------- /api/auto (B 扩展：跟随开关) ----------------
static esp_err_t api_auto_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        int v = (int)qs_float(req, "follow", -1);
        if (v == 0 || v == 1) eda_visualizer_set_auto_follow(v == 1);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "success", (v == 0 || v == 1));
        cJSON_AddBoolToObject(r, "auto_follow", eda_visualizer_is_auto_follow());
        return send_json(req, r);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "auto_follow", eda_visualizer_is_auto_follow());
    return send_json(req, r);
}

// ---------------- /api/post (后处理) ----------------
static esp_err_t api_post_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        float gamma = qs_float(req, "gamma", 1.0f);
        int gate = (int)qs_float(req, "gate", 0.0f);
        float after = qs_float(req, "afterimage", 0.0f);
        if (gate < 0) {
            gate = 0;
        }
        if (gate > 64) {
            gate = 64;
        }
        eda_visualizer_set_post(gamma, (uint8_t)gate, after);
    }
    core_status_t st; dual_core_com_get_status(&st);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "gamma", st.gamma);
    cJSON_AddNumberToObject(r, "gate", st.gate);
    cJSON_AddNumberToObject(r, "afterimage", st.afterimage);
    cJSON_AddBoolToObject(r, "success", true);
    return send_json(req, r);
}

// ---------------- /api/fx (统一效果参数) ----------------
static esp_err_t api_fx_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        led_fx_t fx;
        core_status_t st; dual_core_com_get_status(&st);
        fx = st.fx;   // 以当前值为基准，覆盖请求里带的字段
        char q[160] = {0};
        bool has = (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK);
        if (has) {
            fx.speed       = qs_float(req, "speed", fx.speed);
            fx.intensity   = qs_float(req, "intensity", fx.intensity);
            fx.sensitivity = qs_float(req, "sensitivity", fx.sensitivity);
            fx.hue         = qs_float(req, "hue", fx.hue);
            fx.color_speed = qs_float(req, "color_speed", fx.color_speed);
            fx.beat_react  = qs_float(req, "beat_react", fx.beat_react);
            eda_visualizer_set_fx(&fx);
        }
    }
    core_status_t st; dual_core_com_get_status(&st);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "speed", st.fx.speed);
    cJSON_AddNumberToObject(r, "intensity", st.fx.intensity);
    cJSON_AddNumberToObject(r, "sensitivity", st.fx.sensitivity);
    cJSON_AddNumberToObject(r, "hue", st.fx.hue);
    cJSON_AddNumberToObject(r, "color_speed", st.fx.color_speed);
    cJSON_AddNumberToObject(r, "beat_react", st.fx.beat_react);
    cJSON_AddBoolToObject(r, "success", true);
    return send_json(req, r);
}

// ---------------- /api/spectrum (32 段) ----------------
static esp_err_t api_spectrum_handler(httpd_req_t *req) {
    uint8_t bands[SPEC_BANDS];
    eda_visualizer_get_spectrum(bands);
    cJSON *r = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(r, "bands");
    for (int i = 0; i < SPEC_BANDS; i++)
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(bands[i]));
    return send_json(req, r);
}

// ---------------- /api/source (mic / wifi) ----------------
static esp_err_t api_source_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        char q[32] = {0};
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
            char s[8] = {0};
            if (httpd_query_key_value(q, "src", s, sizeof(s)) == ESP_OK) {
                eda_visualizer_set_audio_source(!strcmp(s, "wifi") ? EDA_AUDIO_SRC_WIFI : EDA_AUDIO_SRC_MIC);
            }
        }
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "source",
        eda_visualizer_get_audio_source() == EDA_AUDIO_SRC_WIFI ? "wifi" : "mic");
    cJSON_AddBoolToObject(r, "streaming", wifi_audio_streaming());
    char ip[16] = {0};
    if (sta_ip(ip, sizeof(ip))) cJSON_AddStringToObject(r, "ip", ip);
    cJSON_AddNumberToObject(r, "port", 5004);
    return send_json(req, r);
}

// ---------------- /api/music (音乐模式：AI对话 <-> 音乐可视化 互斥) ----------------
static esp_err_t api_music_handler(httpd_req_t *req) {
    if (req->method == HTTP_POST || req->method == HTTP_PUT) {
        int on = (int)qs_float(req, "on", -1);
        if (on == 1) {
            eda_visualizer_enter_music_mode();
        } else if (on == 0) {
            eda_visualizer_exit_music_mode();
        }
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "music_mode", eda_visualizer_is_music_mode());
    cJSON_AddBoolToObject(r, "streaming", wifi_audio_streaming());
    cJSON_AddStringToObject(r, "source",
        eda_visualizer_get_audio_source() == EDA_AUDIO_SRC_WIFI ? "wifi" : "mic");
    return send_json(req, r);
}

// ---------------- /loopback.py 与 /start.bat (一键推流) ----------------
static esp_err_t loopback_py_handler(httpd_req_t *req) {
    add_cors(req);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=loopback.py");
    return httpd_resp_send(req, loopback_py_start,
                           (size_t)(loopback_py_end - loopback_py_start));
}

static esp_err_t start_bat_handler(httpd_req_t *req) {
    char ip[16] = {0};
    if (!sta_ip(ip, sizeof(ip))) strcpy(ip, "127.0.0.1");
    char bat[512];
    int n = snprintf(bat, sizeof(bat),
        "@echo off\r\n"
        "curl -s \"http://%s/loopback.py\" -o \"%%TEMP%%\\esp_loopback.py\"\r\n"
        "echo Installing Python deps (first time needs internet)...\r\n"
        "python -m pip install pyaudiowpatch numpy -q\r\n"
        "echo Starting stream. Close this window to stop.\r\n"
        "python \"%%TEMP%%\\esp_loopback.py\" %s 5004\r\n"
        "if errorlevel 1 ( echo Failed. Make sure Python is installed and on PATH. )\r\n"
        "pause\r\n", ip, ip);
    add_cors(req);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=start_esp_audio.bat");
    return httpd_resp_send(req, bat, n);
}

// ---------------- CORS preflight ----------------
static esp_err_t options_handler(httpd_req_t *req) {
    add_cors(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// ---------------- server 生命周期 ----------------
#define REG_ONE(u) do { esp_err_t e_ = httpd_register_uri_handler(s_server, &(u)); \
        if (e_ != ESP_OK) ESP_LOGE(TAG, "register %s failed: %s", (u).uri, esp_err_to_name(e_)); } while(0)
#define REG_GET(path, fn) do { httpd_uri_t u = {.uri=path,.method=HTTP_GET,.handler=fn}; REG_ONE(u);} while(0)
#define REG_ANY(path, fn) do { REG_GET(path,fn); { httpd_uri_t p={.uri=path,.method=HTTP_POST,.handler=fn}; REG_ONE(p);} { httpd_uri_t o={.uri=path,.method=HTTP_OPTIONS,.handler=options_handler}; REG_ONE(o);} } while(0)

static esp_err_t start_server(void) {
    if (s_server) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CONSOLE_PORT;
    cfg.ctrl_port = 32767;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 48;    // default 8 is far too small for /api/* x methods + OTA
    cfg.max_open_sockets = 7;    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        s_server = NULL;
        return ESP_FAIL;
    }

    REG_GET("/", root_handler);
    REG_GET("/ping", ping_handler);
    REG_ANY("/api/status", api_status_handler);
    REG_ANY("/api/mode", api_mode_handler);
    REG_ANY("/api/brightness", api_brightness_handler);
    REG_ANY("/api/command", api_command_handler);
    REG_ANY("/api/auto", api_auto_handler);
    REG_ANY("/api/post", api_post_handler);
    REG_ANY("/api/fx", api_fx_handler);
    REG_ANY("/api/spectrum", api_spectrum_handler);
    REG_ANY("/api/source", api_source_handler);
    REG_ANY("/api/music", api_music_handler);
    REG_GET("/loopback.py", loopback_py_handler);
    REG_GET("/start.bat", start_bat_handler);

    eda_lan_ota_register(s_server);   // /api/ota

    char ip[16] = {0};
    sta_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "Web console ready: http://%s/  (OTA: POST /api/ota)", ip[0] ? ip : "<ip>");
    return ESP_OK;
}

static void stop_server(void) {
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
}

// ------------------------------------------------------------------
// 事件回调里绝不做重活（httpd_start/stop、socket 都会阻塞几百 ms，
// 会卡死 default event loop，干扰 esp_wifi/esp_netif 内部状态机）：
// 只做位标记 + 通知，真正的启停在 worker 任务里执行。
// ------------------------------------------------------------------
#define ACT_GOT_IP   (1u << 0)
#define ACT_STA_DOWN (1u << 1)

static TaskHandle_t s_worker = NULL;

static void console_worker(void *arg) {
    uint32_t acts = 0;
    while (true) {
        xTaskNotifyWait(0, ULONG_MAX, &acts, portMAX_DELAY);
        if (acts & ACT_STA_DOWN) {
            stop_server();
        }
        if (acts & ACT_GOT_IP) {
            eda_lan_ota_confirm_image();
            start_server();
        }
    }
}

static void on_ip_got(void *arg, esp_event_base_t base, int32_t id, void *data) {
    xTaskNotify(s_worker, ACT_GOT_IP, eSetBits);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    xTaskNotify(s_worker, ACT_STA_DOWN, eSetBits);
}

void eda_web_console_init(void) {
    if (s_worker == NULL) {
        xTaskCreate(console_worker, "eda_webw", 6144, NULL, 4, &s_worker);
    }
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_got, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_event, NULL);
    ESP_LOGI(TAG, "Web console initialized (CONFIG_EDA_DEV_MODE=y)");
}

#else  // !CONFIG_EDA_DEV_MODE

void eda_web_console_init(void) {}

#endif
