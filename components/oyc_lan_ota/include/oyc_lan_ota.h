#ifndef OYC_LAN_OTA_H
#define OYC_LAN_OTA_H

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the OTA endpoints (/api/ota GET/POST/PUT/OPTIONS) on an
 * externally-owned esp_http_server instance (see oyc_web_console).
 */
void oyc_lan_ota_register(httpd_handle_t server);

/**
 * Cancel app-rollback if the currently running image is PENDING_VERIFY.
 * Called once the device proves the fresh image can reach the network.
 */
void oyc_lan_ota_confirm_image(void);

#ifdef __cplusplus
}
#endif

#endif // OYC_LAN_OTA_H
