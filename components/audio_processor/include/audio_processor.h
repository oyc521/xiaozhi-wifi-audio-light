#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// FFT配置
#define FFT_SIZE          512      // FFT点数
#define FFT_OUTPUT_SIZE   (FFT_SIZE / 2)  // 输出点数（对称）
#ifndef NUM_FREQ_BANDS
#define NUM_FREQ_BANDS    32       // 频带数量（与 led_controller 的 32 段显示谱对齐）
#endif

// PCM 环形缓冲容量（int16 样本数），2 的幂
#define AUDIO_PCM_RING_SIZE 4096

// FFT处理器结构体
typedef struct {
    int sample_rate;                        // 采样率
    float frequency_bands[NUM_FREQ_BANDS];  // 频带能量
    float peak_values[NUM_FREQ_BANDS];      // 峰值
    uint32_t peak_times[NUM_FREQ_BANDS];    // 峰值时间
    float total_energy;                     // 总能量
    float spectral_centroid;                // 频谱质心
} fft_processor_t;

/**
 * @brief 初始化 FFT 处理器（不再初始化麦克风；PCM 由外部喂入）
 */
esp_err_t fft_processor_init(fft_processor_t *processor, int sample_rate);

/**
 * @brief 向内部环形缓冲写入采集到的 PCM（来自小智 AudioCodec 的 16k 单声道数据）
 * @return 实际写入的样本数
 */
int audio_processor_feed(const int16_t *data, int samples);

/**
 * @brief 从环形缓冲取一帧 FFT_SIZE 做 FFT，结果写回 processor
 * @return ESP_OK 表示成功计算一帧；ESP_ERR_NOT_FOUND 表示缓冲不足一帧
 */
esp_err_t fft_processor_process(fft_processor_t *processor);

/**
 * @brief 直接对给定缓冲区（>=FFT_SIZE 个样本）做 FFT，不使用环形缓冲
 */
esp_err_t fft_processor_process_buffer(fft_processor_t *processor,
                                        const int16_t *audio_buffer,
                                        int buffer_size);

esp_err_t fft_processor_deinit(fft_processor_t *processor);

const float* fft_get_frequency_bands(fft_processor_t *processor);
float fft_get_spectral_centroid(fft_processor_t *processor);
float fft_get_total_energy(fft_processor_t *processor);

// 当前环形缓冲内可用样本数
int audio_processor_available(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PROCESSOR_H */
