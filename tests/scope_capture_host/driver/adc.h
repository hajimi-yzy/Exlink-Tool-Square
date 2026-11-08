#pragma once
#include <stdint.h>
using esp_err_t = int;
enum {
    ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_TIMEOUT = 1, ESP_ERR_NOT_SUPPORTED = 2,
    ADC_CHANNEL_3 = 3, ADC_ATTEN_DB_11 = 3, ADC_UNIT_1 = 1,
    ADC_WIDTH_BIT_12 = 12, SOC_ADC_DIGI_MAX_BITWIDTH = 12,
    ADC_CONV_SINGLE_UNIT_1 = 1, ADC_DIGI_OUTPUT_FORMAT_TYPE2 = 2
};
struct adc_digi_init_config_t {
    unsigned max_store_buf_size, conv_num_each_intr, adc1_chan_mask;
};
struct adc_digi_pattern_config_t { unsigned atten, channel, unit, bit_width; };
struct adc_digi_configuration_t {
    unsigned pattern_num;
    adc_digi_pattern_config_t *adc_pattern;
    uint32_t sample_freq_hz;
    unsigned conv_mode, format;
};
union adc_digi_output_data_t {
    uint32_t val;
    struct { uint32_t data : 12, channel : 4, unit : 1, reserved : 15; } type2;
};
static_assert(sizeof(adc_digi_output_data_t) == 4, "Mock TYPE2 record must be 4 bytes");
esp_err_t adc_digi_initialize(const adc_digi_init_config_t *);
esp_err_t adc_digi_controller_configure(const adc_digi_configuration_t *);
esp_err_t adc_digi_start();
esp_err_t adc_digi_read_bytes(uint8_t *, uint32_t, uint32_t *, uint32_t);
esp_err_t adc_digi_stop();
esp_err_t adc_digi_deinitialize();
const char *esp_err_to_name(esp_err_t);
