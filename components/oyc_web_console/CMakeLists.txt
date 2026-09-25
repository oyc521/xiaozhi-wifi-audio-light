idf_component_register(SRCS "oyc_web_console.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "assets/console.html" "assets/loopback.py"
                       REQUIRES oyc_lan_ota
                                oyc_visualizer
                                dual_core_com
                                led_controller
                                wifi_audio
                                esp_http_server
                                json
                       PRIV_REQUIRES esp_wifi
                                     esp_netif
                                     esp_event
                                     esp_app_format
                                     esp_timer
                                     freertos)
