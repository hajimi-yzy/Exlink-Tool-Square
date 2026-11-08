#pragma once
#include <stdint.h>
typedef uint32_t spi_flash_mmap_handle_t;
static const int SPI_FLASH_MMAP_DATA = 0;
inline void spi_flash_munmap(spi_flash_mmap_handle_t) {}
