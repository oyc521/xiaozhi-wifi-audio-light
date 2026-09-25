#ifndef WIFI_AUDIO_H
#define WIFI_AUDIO_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t wifi_audio_init(uint16_t port);
int wifi_audio_read(int16_t *out, int n, int timeout_ms);
int wifi_audio_available(void);   // 环形缓冲中当前可读样本数
bool wifi_audio_streaming(void);
bool wifi_audio_get_ip_str(char *out, int out_len);
int wifi_audio_get_port(void);

#endif
