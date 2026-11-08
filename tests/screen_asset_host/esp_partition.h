#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "esp_spi_flash.h"
typedef int esp_partition_subtype_t;
static const int ESP_PARTITION_TYPE_DATA = 1;
static const int ESP_OK = 0;
struct esp_partition_t { size_t size; unsigned slot; };
extern esp_partition_t fakeParts[2];
extern std::vector<uint8_t> fakeFlash[2];
extern unsigned erasedSlot;
inline const esp_partition_t *esp_partition_find_first(int, esp_partition_subtype_t, const char *name) {
    if (!strcmp(name,"assets_a")) return &fakeParts[0];
    if (!strcmp(name,"assets_b")) return &fakeParts[1];
    return nullptr;
}
inline int esp_partition_erase_range(const esp_partition_t *part, size_t offset, size_t size) {
    if (offset > part->size || size > part->size - offset || offset % 4096 || size % 4096) return -1;
    memset(fakeFlash[part->slot].data() + offset, 0xff, size);
    erasedSlot = part->slot; return ESP_OK;
}
inline int esp_partition_write(const esp_partition_t *part, size_t offset, const void *data, size_t size) {
    if (offset > part->size || size > part->size - offset) return -1;
    memcpy(fakeFlash[part->slot].data() + offset,data,size); return ESP_OK;
}
inline int esp_partition_mmap(const esp_partition_t *part, size_t offset, size_t size, int,
                              const void **output, spi_flash_mmap_handle_t *handle) {
    if (offset > part->size || size > part->size - offset) return -1;
    *output = fakeFlash[part->slot].data() + offset;
    *handle = part->slot + 1; return ESP_OK;
}
