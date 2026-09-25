#ifndef OYC_WEB_CONSOLE_H
#define OYC_WEB_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Owns the single port-80 esp_http_server for the xiaozhi-wifi-audio-light dev UI:
 * web console root page, /api/... endpoints, /start.bat + /loopback.py,
 * plus OTA (registered from oyc_lan_ota).
 *
 * Server runs only while the station holds an IP, so port 80 stays free for
 * the esp_wifi-connect captive portal in AP mode.
 * No-op unless CONFIG_OYC_DEV_MODE=y.
 */
void oyc_web_console_init(void);

#ifdef __cplusplus
}
#endif

#endif // OYC_WEB_CONSOLE_H
