#include "scope_capture.h"
#include "scope_math.h"
#include "app_log.h"
#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <freertos/task.h>

namespace {
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t worker;
scope_config_t requested = {2, 1500, SCOPE_RISING};
scope_frame_t latest;
scope_state_t state = SCOPE_STOPPED;
uint32_t generation;
bool wanted, owned, release_failed;

void released(void)
{
    portENTER_CRITICAL(&lock);
    owned = wanted || release_failed;
    // Teardown of an old generation must not overwrite a newer START.
    if (wanted) state = SCOPE_STARTING;
    else if (state != SCOPE_ERROR) state = SCOPE_STOPPED;
    portEXIT_CRITICAL(&lock);
}

bool same_request(uint32_t current)
{
    portENTER_CRITICAL(&lock);
    const bool same = wanted && generation == current;
    portEXIT_CRITICAL(&lock);
    return same;
}

void fail(uint32_t current, esp_err_t error)
{
    portENTER_CRITICAL(&lock);
    if (generation == current) { wanted = false; state = SCOPE_ERROR; }
    else state = wanted ? SCOPE_STARTING : SCOPE_STOPPED;
    owned = wanted;
    portEXIT_CRITICAL(&lock);
    APP_LOGE("scope", "ADC capture failed: %s\r\n", esp_err_to_name(error));
}

bool finish_adc(bool started)
{
    const esp_err_t stopped = started ? adc_digi_stop() : ESP_OK;
    const esp_err_t error = adc_digi_deinitialize();
    if (stopped != ESP_OK)
        APP_LOGW("scope", "ADC stop state: %s\r\n", esp_err_to_name(stopped));
    if (error == ESP_OK) return true;
    // A driver lifecycle error must not expose ADC1 to battery oneshot or
    // initialize a second controller. Normal stop/deinit is not retried.
    portENTER_CRITICAL(&lock);
    release_failed = owned = true;
    wanted = false;
    state = SCOPE_ERROR;
    ++generation;
    portEXIT_CRITICAL(&lock);
    APP_LOGE("scope", "ADC release failed: %s; reboot required\r\n", esp_err_to_name(error));
    return false;
}

void capture_worker(void *)
{
    uint16_t samples[2 * SCOPE_MAX_POINTS];
    alignas(4) uint8_t bytes[512];
    scope_frame_t frame = {};
    esp_adc_cal_characteristics_t calibration = {};
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&lock);
        const bool run = wanted;
        const uint32_t current = generation;
        const scope_config_t config = requested;
        portEXIT_CRITICAL(&lock);
        if (!run) { released(); continue; }
        if (!same_request(current)) continue;
        frame.calibrated = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11,
            ADC_WIDTH_BIT_12, 1100, &calibration) == ESP_ADC_CAL_VAL_EFUSE_TP_FIT;
        if (!frame.calibrated) { fail(current, ESP_ERR_NOT_SUPPORTED); continue; }
        // GPIO4 = ADC1 channel 3 on ESP32-S3. Use the pinned IDF 4.4 API.
        adc_digi_init_config_t init = {};
        init.max_store_buf_size = 8192;
        init.conv_num_each_intr = sizeof(bytes);
        init.adc1_chan_mask = 1U << ADC_CHANNEL_3;
        esp_err_t error = adc_digi_initialize(&init);
        if (error != ESP_OK) { fail(current, error); continue; }
        adc_digi_pattern_config_t pattern = {};
        pattern.atten = ADC_ATTEN_DB_11;
        pattern.channel = ADC_CHANNEL_3;
        pattern.unit = ADC_UNIT_1 - 1;
        pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
        adc_digi_configuration_t cfg = {};
        cfg.pattern_num = 1;
        cfg.adc_pattern = &pattern;
        cfg.sample_freq_hz = scope_sample_hz(config.time_div_ms);
        cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
        cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
        error = adc_digi_controller_configure(&cfg);
        if (error == ESP_OK) error = adc_digi_start();
        if (error != ESP_OK) {
            if (finish_adc(false)) fail(current, error);
            continue;
        }
        portENTER_CRITICAL(&lock);
        if (wanted && generation == current) state = SCOPE_RUNNING;
        portEXIT_CRITICAL(&lock);
        APP_LOGI("scope", "RUN GPIO4 ADC1_CH3: %lu Sa/s, %u ms/div\r\n",
                 (unsigned long)cfg.sample_freq_hz, config.time_div_ms);
        const uint16_t count = scope_point_count(config.time_div_ms);
        uint16_t used = 0;
        unsigned failures = 0;
        while (same_request(current)) {
            uint32_t length = 0;
            error = adc_digi_read_bytes(bytes, sizeof(bytes), &length, 10);
            if (error == ESP_ERR_TIMEOUT) continue;
            if (error != ESP_OK) {
                // Never join samples across a DMA overflow into a false time axis.
                used = 0;
                ++frame.dropped;
                if (++failures >= 10) break;
                continue;
            }
            failures = 0;
            for (unsigned offset = 0; offset + sizeof(adc_digi_output_data_t) <= length;
                 offset += sizeof(adc_digi_output_data_t)) {
                const auto *sample = reinterpret_cast<const adc_digi_output_data_t *>(bytes + offset);
                if (sample->type2.unit || sample->type2.channel != ADC_CHANNEL_3) {
                    used = 0;
                    continue;
                }
                samples[used++] = esp_adc_cal_raw_to_voltage(sample->type2.data, &calibration);
                if (used < 2 * count) continue;
                scope_analyze(samples, count, cfg.sample_freq_hz, &config, &frame);
                portENTER_CRITICAL(&lock);
                if (wanted && generation == current) {
                    frame.sequence = latest.sequence + 1;
                    latest = frame;
                }
                portEXIT_CRITICAL(&lock);
                used = 0;
            }
        }
        if (!finish_adc(true)) continue;
        if (failures >= 10) fail(current, error);
        else released();
        APP_LOGI("scope", "STOP; ADC released, dropped=%lu, stack=%u\r\n",
                 (unsigned long)frame.dropped, (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        // A configuration/start request made during teardown remains notified.
    }
}
}

extern "C" bool scope_acq_start(const scope_config_t *config)
{
    if (!config || !scope_sample_hz(config->time_div_ms) ||
        config->trigger < SCOPE_FREE || config->trigger > SCOPE_FALLING)
        return false;
    portENTER_CRITICAL(&lock);
    const bool blocked = release_failed;
    portEXIT_CRITICAL(&lock);
    if (blocked) return false;
    if (!worker && xTaskCreatePinnedToCore(capture_worker, "scope_adc", 8192, nullptr, 1,
                                          &worker, 0) != pdPASS) return false;
    portENTER_CRITICAL(&lock);
    if (release_failed) { portEXIT_CRITICAL(&lock); return false; }
    requested = *config;
    wanted = true;
    // Reserve ADC1 before returning so battery oneshot cannot race startup.
    owned = true;
    state = SCOPE_STARTING;
    ++generation;
    latest.count = 0;
    portEXIT_CRITICAL(&lock);
    xTaskNotifyGive(worker);
    return true;
}

extern "C" void scope_acq_stop(void)
{
    portENTER_CRITICAL(&lock);
    wanted = false;
    ++generation;
    portEXIT_CRITICAL(&lock);
    if (worker) xTaskNotifyGive(worker);
}

extern "C" bool scope_adc_owned(void)
{
    portENTER_CRITICAL(&lock);
    const bool result = owned || wanted;
    portEXIT_CRITICAL(&lock);
    return result;
}

extern "C" bool scope_acq_quiesce(void)
{
    scope_acq_stop();
    for (unsigned i = 0; i < 20 && scope_adc_owned(); ++i) vTaskDelay(pdMS_TO_TICKS(5));
    return !scope_adc_owned();
}

extern "C" bool scope_acq_resume(void)
{
    portENTER_CRITICAL(&lock);
    const scope_config_t config = requested;
    portEXIT_CRITICAL(&lock);
    return scope_acq_start(&config);
}

extern "C" scope_state_t scope_acq_state(void)
{
    portENTER_CRITICAL(&lock);
    const scope_state_t result = state;
    portEXIT_CRITICAL(&lock);
    return result;
}

extern "C" bool scope_acq_read(scope_frame_t *frame, uint32_t after_sequence)
{
    portENTER_CRITICAL(&lock);
    const bool fresh = latest.count && latest.sequence != after_sequence;
    if (fresh) *frame = latest;
    portEXIT_CRITICAL(&lock);
    return fresh;
}
