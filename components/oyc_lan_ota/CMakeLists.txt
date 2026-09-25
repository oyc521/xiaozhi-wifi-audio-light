idf_component_register(SRCS "oyc_lan_ota.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_server
                                     esp_app_format
                                     app_update
                                     bootloader_support
                                     esp_partition
                                     esp_netif
                                     esp_wifi
                                     nvs_flash)
