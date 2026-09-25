/*
 * audio_processor —— 移植自 FFTvisiual1.0ws2812b，去除 INMP441 麦克风 / WiFi 推流依赖。
 * 音频改为由小智 AudioCodec 已采集的 16k 单声道 PCM 通过 audio_processor_feed() 灌入。
 */
#include "audio_processor.h"
#include "utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// ESP-DSP库
#include "dsps_fft2r.h"
#include "dsps_wind.h"

static const char *TAG = "AUDIO_PROC";

// FFT缓冲区
static float fft_input[FFT_SIZE * 2];               // 复数数组：实部+虚部
static float fft_magnitude[FFT_OUTPUT_SIZE + 1];    // 含直流与奈奎斯特点

// PCM 环形缓冲（单生产者 feed / 单消费者 process）
static int16_t s_ring[AUDIO_PCM_RING_SIZE];
static volatile uint32_t s_head = 0;   // 写入位置
static volatile uint32_t s_tail = 0;   // 读取位置
static int16_t s_frame[FFT_SIZE];      // 取帧暂存

// 一阶高通滤波器，截止频率约20Hz
static void high_pass_filter(int16_t *buffer, int size, float cutoff_freq, float sample_rate) {
    static float prev_input = 0;
    static float prev_output = 0;

    float dt = 1.0f / sample_rate;
    float RC = 1.0f / (2 * M_PI * cutoff_freq);
    float alpha = RC / (RC + dt);

    for (int i = 0; i < size; i++) {
        float input = buffer[i];
        float output = alpha * prev_output + alpha * (input - prev_input);
        buffer[i] = (int16_t)output;
        prev_input = input;
        prev_output = output;
    }
}

// 使用ESP-DSP进行FFT计算
static void compute_fft_with_dsp(const int16_t *input, float *magnitude) {
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input[i * 2] = (float)input[i];   // 实部
        fft_input[i * 2 + 1] = 0.0f;          // 虚部
    }

    dsps_fft2r_fc32(fft_input, FFT_SIZE);
    dsps_bit_rev_fc32(fft_input, FFT_SIZE);

    for (int i = 0; i <= FFT_OUTPUT_SIZE; i++) {
        float real = fft_input[i * 2];
        float imag = fft_input[i * 2 + 1];
        magnitude[i] = sqrtf(real * real + imag * imag) / FFT_SIZE;
    }
}

esp_err_t fft_processor_init(fft_processor_t *processor, int sample_rate) {
    if (!processor) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "初始化FFT处理器 (FFT=%d, bands=%d, rate=%dHz)", FFT_SIZE, NUM_FREQ_BANDS, sample_rate);

    memset(processor, 0, sizeof(fft_processor_t));
    processor->sample_rate = sample_rate;

    s_head = s_tail = 0;

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-DSP初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "FFT处理器初始化成功");
    return ESP_OK;
}

int audio_processor_available(void) {
    return (int)(s_head - s_tail);   // uint 回绕差值即为可用样本数（head≥tail 逻辑上单调）
}

int audio_processor_feed(const int16_t *data, int samples) {
    if (!data || samples <= 0) return 0;

    uint32_t head = s_head;
    uint32_t tail = s_tail;
    int free_space = (int)(AUDIO_PCM_RING_SIZE - (head - tail));

    // 缓冲过满时丢弃最旧数据（保实时性，容忍跳变）
    if (samples > free_space) {
        int drop = samples - free_space;
        tail += drop;
        ESP_LOGD(TAG, "环形缓冲溢出，丢弃最旧 %d 样本", drop);
    }

    int written = 0;
    while (written < samples) {
        uint32_t idx = (head + written) & (AUDIO_PCM_RING_SIZE - 1);
        s_ring[idx] = data[written];
        written++;
    }
    s_head = head + written;
    s_tail = tail;
    return written;
}

// 核心：对一帧 buffer（FFT_SIZE 个样本）做 FFT + 频带能量
static esp_err_t fft_process_frame(fft_processor_t *processor, int16_t *buf) {
    high_pass_filter(buf, FFT_SIZE, 20.0f, (float)processor->sample_rate);
    compute_fft_with_dsp(buf, fft_magnitude);

    float freq_resolution = (float)processor->sample_rate / FFT_SIZE;

    float band_edges[NUM_FREQ_BANDS + 1];
    float log_min = log10f(20.0f);
    float log_max = log10f(8000.0f);
    float log_step = (log_max - log_min) / NUM_FREQ_BANDS;
    for (int i = 0; i <= NUM_FREQ_BANDS; i++) {
        band_edges[i] = powf(10.0f, log_min + i * log_step);
    }

    processor->total_energy = 0;

    // 把 FFT bin 连续分配给各频带（bin→band）：
    //   1) 每段与上一段不重叠（start >= prev_end）
    //   2) 每段至少覆盖 1 个 bin（end > start），避免低频段带宽 < 1 个 bin 时 count=0 恒为 0
    //   3) 跳过 DC(bin0)，避免直流泄漏污染最低频段
    int prev_end = 1;
    for (int band = 0; band < NUM_FREQ_BANDS; band++) {
        int start_bin = (int)(band_edges[band] / freq_resolution);
        int end_bin   = (int)(band_edges[band + 1] / freq_resolution);

        if (start_bin < prev_end) start_bin = prev_end;      // 不与上一段重叠
        if (start_bin > FFT_OUTPUT_SIZE - 1) start_bin = FFT_OUTPUT_SIZE - 1;
        if (end_bin <= start_bin) end_bin = start_bin + 1;   // 至少覆盖 1 个 bin
        if (end_bin > FFT_OUTPUT_SIZE) end_bin = FFT_OUTPUT_SIZE;

        float band_energy = 0;
        int count = 0;
        for (int bin = start_bin; bin < end_bin; bin++) {
            band_energy += fft_magnitude[bin];
            count++;
        }
        if (count > 0) {
            band_energy /= count;
            band_energy *= 100.0f;
        }

        processor->frequency_bands[band] =
            0.2f * processor->frequency_bands[band] + 0.8f * band_energy;
        processor->total_energy += processor->frequency_bands[band];

        prev_end = end_bin;
    }

    if (processor->total_energy > 0) {
        float centroid_sum = 0;
        for (int i = 0; i < NUM_FREQ_BANDS; i++) {
            float center_freq = sqrtf(band_edges[i] * band_edges[i + 1]);
            centroid_sum += center_freq * processor->frequency_bands[i];
        }
        processor->spectral_centroid = centroid_sum / processor->total_energy;
    } else {
        processor->spectral_centroid = 0.0f;
    }

    return ESP_OK;
}

esp_err_t fft_processor_process(fft_processor_t *processor) {
    if (!processor) return ESP_ERR_INVALID_ARG;
    if (audio_processor_available() < FFT_SIZE) return ESP_ERR_NOT_FOUND;

    uint32_t tail = s_tail;
    for (int i = 0; i < FFT_SIZE; i++) {
        s_frame[i] = s_ring[(tail + i) & (AUDIO_PCM_RING_SIZE - 1)];
    }
    s_tail = tail + FFT_SIZE;

    return fft_process_frame(processor, s_frame);
}

esp_err_t fft_processor_process_buffer(fft_processor_t *processor,
                                       const int16_t *audio_buffer,
                                       int buffer_size) {
    if (!processor || !audio_buffer || buffer_size < FFT_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    // 复制一份，避免破坏调用者数据（high_pass 会就地修改）
    int16_t tmp[FFT_SIZE];
    memcpy(tmp, audio_buffer, FFT_SIZE * sizeof(int16_t));
    return fft_process_frame(processor, tmp);
}

const float* fft_get_frequency_bands(fft_processor_t *processor) {
    if (!processor) return NULL;
    return processor->frequency_bands;
}

float fft_get_total_energy(fft_processor_t *processor) {
    if (!processor) return 0.0f;
    return processor->total_energy;
}

float fft_get_spectral_centroid(fft_processor_t *processor) {
    if (!processor) return 0.0f;
    return processor->spectral_centroid;
}

esp_err_t fft_processor_deinit(fft_processor_t *processor) {
    dsps_fft2r_deinit_fc32();
    if (processor) memset(processor, 0, sizeof(fft_processor_t));
    return ESP_OK;
}
