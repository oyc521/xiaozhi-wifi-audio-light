idf_component_register(SRCS "eda_visualizer.c"
                       INCLUDE_DIRS "include"
                       REQUIRES audio_processor
                                led_controller
                                dual_core_com
                                wifi_audio
                                esp_event
                                esp_netif)
