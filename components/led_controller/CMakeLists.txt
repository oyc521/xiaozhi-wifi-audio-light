# led_controller: ported from FFTvisiual1.0ws2812b.
# Strip driver uses xiaozhi's managed espressif__led_strip (RMT).
idf_component_register(SRCS "led_controller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES utils
                                espressif__led_strip
                                esp_driver_rmt
                                esp_driver_gpio
                                esp_timer
                                esp_hw_support
                       PRIV_REQUIRES freertos)
