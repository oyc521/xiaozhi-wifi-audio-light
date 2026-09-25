/*
 * EDA LAN OTA - firmware uploader URI provider for xiaozhi-wifi-audio-light.
 *
 * Provides /api/ota endpoints and registers them onto the single port-80
 * esp_http_server owned by eda_web_console (mirrors the A-project split of
 * wifi_core(own server) + ota_updater(register only)).
 *
 *   POST/PUT /api/ota  raw "xiaozhi.bin" body -> write inactive OTA partition,
 *                      set boot partition, reboot.
 *   GET  /api/ota      JSON status (running partition, app version).
 *
 * Rollback: IDF app-rollback is enabled; the new image boots in PENDING_VERIFY.
 * eda_lan_ota_confirm_image() cancels rollback once WiFi is up, and the normal
 * xiaozhi boot path (Ota::MarkCurrentVersionValid) confirms as well.
 *
 * Gated behind CONFIG_EDA_DEV_MODE (disable for production).
 */

#include "eda_lan_ota.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

#include "sdkconfig.h"

#if CONFIG_EDA_DEV_MODE

#define TAG "EDA_LAN_OTA"
#define OTA_RECV_BUF_SIZE 4096

static bool s_ota_busy = false;
static bool s_ota_done = false;

// ------------------------------------------------------------------
// Reboot after a short delay so the HTTP response can flush to client
// ------------------------------------------------------------------
static void ota_reboot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2500));
    esp_restart();
    vTaskDelete(NULL);
}

static void schedule_reboot(void) {
    xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
}

// ------------------------------------------------------------------
// CORS preflight (lets a browser-based console POST from another origin)
// ------------------------------------------------------------------
static esp_err_t cors_preflight_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// ------------------------------------------------------------------
// GET /api/ota - status
// ------------------------------------------------------------------
static esp_err_t ota_status_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app = esp_app_get_description();
    const char *label = running ? running->label : "unknown";

    // 当前运行镜像的验证状态：pending_verify 表示尚未确认，重启会被 bootloader 回滚
    const char *state_str = "unknown";
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        switch (state) {
        case ESP_OTA_IMG_NEW:            state_str = "new"; break;
        case ESP_OTA_IMG_PENDING_VERIFY: state_str = "pending_verify"; break;
        case ESP_OTA_IMG_VALID:          state_str = "valid"; break;
        case ESP_OTA_IMG_INVALID:        state_str = "invalid"; break;
        case ESP_OTA_IMG_ABORTED:        state_str = "aborted"; break;
        case ESP_OTA_IMG_UNDEFINED:      state_str = "undefined"; break;
        default:                         state_str = "unknown"; break;
        }
    }

    char buf[320];
    int n = snprintf(buf, sizeof(buf),
                     "{\"status\":\"%s\",\"running_partition\":\"%s\","
                     "\"app_version\":\"%s\",\"app_project\":\"%s\",\"flavor\":\"webconsole-v2\","
                     "\"image_state\":\"%s\"}",
                     s_ota_busy ? "busy" : (s_ota_done ? "rebooting" : "idle"),
                     label, app ? app->version : "?",
                     app ? app->project_name : "?", state_str);
    if (n < 0) n = 0;
    return httpd_resp_send(req, buf, n);
}

// ------------------------------------------------------------------
// POST /api/ota - raw firmware image upload
// ------------------------------------------------------------------
static esp_err_t ota_upload_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (s_ota_busy) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "OTA already in progress");
        return ESP_OK;
    }
    s_ota_busy = true;
    s_ota_done = false;

    esp_err_t result = ESP_OK;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(running);
    if (update == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "No OTA partition");
        goto done;
    }
    ESP_LOGI(TAG, "OTA target: %s @0x%x size 0x%x", update->label, update->address,
             update->size);

    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "411 Length Required");
        httpd_resp_sendstr(req, "Content-Length required");
        goto done;
    }
    if (req->content_len > (long)update->size) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "Firmware larger than OTA partition");
        goto done;
    }

    esp_ota_handle_t ota_handle;
    result = esp_ota_begin(update, OTA_SIZE_UNKNOWN, &ota_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(result));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "esp_ota_begin failed");
        goto done;
    }

    char *buf = malloc(OTA_RECV_BUF_SIZE);
    if (buf == NULL) {
        esp_ota_abort(ota_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        goto done;
    }

    size_t written = 0;
    bool fail = false;
    int received;
    while (1) {
        received = httpd_req_recv(req, buf, OTA_RECV_BUF_SIZE);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGW(TAG, "Socket error/EOF during OTA receive: %d", received);
            fail = true;
            break;
        }
        result = esp_ota_write(ota_handle, buf, received);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(result));
            fail = true;
            break;
        }
        written += received;
        if (written >= (size_t)req->content_len) {
            break;
        }
    }
    free(buf);

    if (fail) {
        esp_ota_abort(ota_handle);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "OTA upload interrupted");
        goto done;
    }
    if (written != (size_t)req->content_len) {
        ESP_LOGE(TAG, "Size mismatch: got %u expected %d", (unsigned)written,
                 (int)req->content_len);
        esp_ota_abort(ota_handle);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Size mismatch");
        goto done;
    }

    result = esp_ota_end(ota_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end (image validation) failed: %s", esp_err_to_name(result));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Firmware image invalid");
        goto done;
    }

    result = esp_ota_set_boot_partition(update);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(result));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Set boot partition failed");
        goto done;
    }

    s_ota_done = true;
    ESP_LOGI(TAG, "OTA success: %u bytes -> %s, rebooting...", (unsigned)written,
             update->label);
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"reboot\":true}");
    schedule_reboot();

done:
    s_ota_busy = false;
    return ESP_OK;
}

static const httpd_uri_t uri_ota_post = {
    .uri = "/api/ota",
    .method = HTTP_POST,
    .handler = ota_upload_handler,
};
static const httpd_uri_t uri_ota_put = {
    .uri = "/api/ota",
    .method = HTTP_PUT,
    .handler = ota_upload_handler,
};
static const httpd_uri_t uri_ota_get = {
    .uri = "/api/ota",
    .method = HTTP_GET,
    .handler = ota_status_handler,
};
static const httpd_uri_t uri_ota_options = {
    .uri = "/api/ota",
    .method = HTTP_OPTIONS,
    .handler = cors_preflight_handler,
};

void eda_lan_ota_register(httpd_handle_t server) {
    if (server == NULL) return;
    httpd_register_uri_handler(server, &uri_ota_post);
    httpd_register_uri_handler(server, &uri_ota_put);
    httpd_register_uri_handler(server, &uri_ota_get);
    httpd_register_uri_handler(server, &uri_ota_options);
}

// ------------------------------------------------------------------
// Cancel pending rollback once the running image proves it can reach the network.
// esp_ota_mark_app_valid_cancel_rollback() is safe to call repeatedly.
// ------------------------------------------------------------------
void eda_lan_ota_confirm_image(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "Confirmed new image (%s)", esp_err_to_name(ret));
        }
    }
}

#else  // !CONFIG_EDA_DEV_MODE

void eda_lan_ota_register(httpd_handle_t server) { (void)server; }
void eda_lan_ota_confirm_image(void) {}

#endif
