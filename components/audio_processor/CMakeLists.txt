# audio_processor: ported from FFTvisiual1.0ws2812b, decoupled from
# inmp441_mic / wifi_audio. PCM is fed externally (xiaozhi mic tap).
idf_component_register(SRCS "audio_processor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES utils
                       PRIV_REQUIRES espressif__esp-dsp
                                     esp_timer
                                     freertos)
