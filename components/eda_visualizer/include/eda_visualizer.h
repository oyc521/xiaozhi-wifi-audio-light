#ifndef EDA_VISUALIZER_H
#define EDA_VISUALIZER_H

#include <stdint.h>
#include "esp_err.h"
#include "led_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the ambient-light visualizer engine.
 *
 * Owns the WS2812 strip on `gpio` (RMT), initializes FFT + led_controller,
 * and runs a low-priority rendering task that consumes 16 kHz mono PCM fed
 * through eda_visualizer_feed().
 */
esp_err_t eda_visualizer_start(int gpio, int led_num, int brightness_percent);

/**
 * Feed 16 kHz mono PCM (called from the xiaozhi audio input task).
 * Lock-free, safe on both cores.
 */
void eda_visualizer_feed(const int16_t *pcm, int samples);

/**
 * Switch the shared display mode (single source of truth, queued).
 * Explicit user choice: also DISABLES auto-follow of device state,
 * so state transitions will not overwrite it (see _auto / set_auto_follow).
 */
void eda_visualizer_set_mode(led_mode_t mode);

/**
 * State-driven mode change: only applied while auto-follow is enabled.
 */
void eda_visualizer_set_mode_auto(led_mode_t mode);

/** Enable/disable auto-follow of device state (true = follow, default). */
void eda_visualizer_set_auto_follow(bool follow);
bool eda_visualizer_is_auto_follow(void);

/** Map a xiaozhi emotion string ("happy"/"sad"/...) to a light scene (T3). */
void eda_visualizer_set_emotion(const char *emotion);

/** Set global brightness 0-100 (queued). */
void eda_visualizer_set_brightness(int percent);

/** Set global post-processing (queued): gamma>=1, noise_gate 0-64, afterimage 0-0.9 */
void eda_visualizer_set_post(float gamma, uint8_t noise_gate, float afterimage);

/** Set unified effect params (queued). */
void eda_visualizer_set_fx(const led_fx_t *fx);

/** Read the 32-segment normalized display spectrum (0-255 each). */
void eda_visualizer_get_spectrum(uint8_t *out32);

/* --- read-only status accessors (safe to call from main/ TUs; avoids
       pulling audio_processor.h which collides with xiaozhi's own header) --- */
led_mode_t eda_visualizer_get_mode(void);
int        eda_visualizer_get_brightness(void);
int        eda_visualizer_get_bpm(void);

/** FFT audio source. WIFI = PC pushes 44.1k mono PCM over UDP (port 5004).
    Auto falls back to mic while no stream packets are arriving. */
typedef enum {
    EDA_AUDIO_SRC_MIC = 0,
    EDA_AUDIO_SRC_WIFI = 1,
} eda_audio_src_t;

void            eda_visualizer_set_audio_source(eda_audio_src_t src);
eda_audio_src_t eda_visualizer_get_audio_source(void);
/** true = WiFi source selected AND stream currently flowing */
bool            eda_visualizer_audio_streaming(void);

#ifdef __cplusplus
}
#endif

#endif // EDA_VISUALIZER_H
