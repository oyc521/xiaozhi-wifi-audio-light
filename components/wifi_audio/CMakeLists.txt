# wifi_audio: UDP music-stream receiver, ported from FFTvisiual1.0ws2812b.
idf_component_register(SRCS "wifi_audio.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip
                                     esp_netif
                                     esp_timer
                                     freertos)
