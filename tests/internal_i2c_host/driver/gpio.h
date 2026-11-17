#pragma once
#include "fake_host.h"
using gpio_num_t = int;
constexpr int ESP_OK = 0;
constexpr int GPIO_INTR_NEGEDGE = 2;
inline int gpio_intr_disable(gpio_num_t) {
  host::runtime.irq_enabled = false; ++host::runtime.irq_disables; return ESP_OK;
}
inline int gpio_set_intr_type(gpio_num_t, int type) {
  host::runtime.irq_type = type; return ESP_OK;
}
inline int gpio_intr_enable(gpio_num_t) {
  host::runtime.irq_enabled = true; ++host::runtime.irq_enables; return ESP_OK;
}
