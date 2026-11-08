#pragma once
#include "driver/adc.h"
struct esp_adc_cal_characteristics_t { unsigned unused; };
enum { ESP_ADC_CAL_VAL_DEFAULT_VREF = 0, ESP_ADC_CAL_VAL_EFUSE_TP = 1,
       ESP_ADC_CAL_VAL_EFUSE_TP_FIT = 2 };
int esp_adc_cal_characterize(unsigned, unsigned, unsigned, unsigned,
                             esp_adc_cal_characteristics_t *);
uint32_t esp_adc_cal_raw_to_voltage(uint32_t, const esp_adc_cal_characteristics_t *);
