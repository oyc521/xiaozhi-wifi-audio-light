#ifndef EDA_LAN_OTA_H
#define EDA_LAN_OTA_H

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the OTA endpoints (/api/ota GET/POST/PUT/OPTIONS) on an
 * externally-owned esp_http_server instance (see eda_web_console).
 */
void eda_lan_ota_register(httpd_handle_t server);

/**
 * Cancel app-rollback if the currently running image is PENDING_VERIFY.
 * Called once the device proves the fresh image can reach the network.
 */
void eda_lan_ota_confirm_image(void);

#ifdef __cplusplus
}
#endif

#endif // EDA_LAN_OTA_H
