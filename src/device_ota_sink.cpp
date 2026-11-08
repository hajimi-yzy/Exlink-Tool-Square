#include "device_ota_sink.h"
#include <string.h>

namespace exlink {

EspOtaSink::EspOtaSink() : partition_(nullptr), handle_(0), writing_(false), ready_(false) {
  mbedtls_sha256_init(&hash_);
}
EspOtaSink::~EspOtaSink() { abort(); mbedtls_sha256_free(&hash_); }

bool EspOtaSink::begin(const UpdateManifest &manifest) {
  abort();
  if (manifest.kind != UpdateKind::Firmware) return false;
  partition_ = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!partition_ || !running || partition_->address == running->address ||
      manifest.size > partition_->size) return false;
  if (mbedtls_sha256_starts_ret(&hash_, 0) != 0) return false;
  // Erase on sequential writes instead of blocking BEGIN while the complete
  // image region is erased. Bounds are enforced above and by UpdateReceiver.
  if (esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES, &handle_) != ESP_OK) return false;
  writing_ = true;
  return true;
}

bool EspOtaSink::write(const uint8_t *data, size_t size) {
  if (!writing_ || !data || !size) return false;
  if (esp_ota_write(handle_, data, size) != ESP_OK) return false;
  return mbedtls_sha256_update_ret(&hash_, data, size) == 0;
}

UpdateResult EspOtaSink::finish(const uint8_t expected[32]) {
  if (!writing_) return UpdateResult::NotReady;
  uint8_t actual[32];
  if (mbedtls_sha256_finish_ret(&hash_, actual) != 0) return UpdateResult::StorageError;
  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(actual); ++i) difference |= actual[i] ^ expected[i];
  if (difference) return UpdateResult::HashMismatch;
  esp_image_header_t header;
  esp_app_desc_t description;
  if (esp_partition_read(partition_, 0, &header, sizeof(header)) != ESP_OK ||
      header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32S3 ||
      esp_ota_get_partition_description(partition_, &description) != ESP_OK ||
      strncmp(description.project_name, "exlink-co5300-31pin", sizeof(description.project_name)) != 0)
    return UpdateResult::InvalidManifest;
  // end() validates the ESP image and frees the handle even on error.
  esp_err_t result = esp_ota_end(handle_);
  writing_ = false;
  handle_ = 0;
  if (result != ESP_OK) return UpdateResult::StorageError;
  ready_ = true;
  return UpdateResult::Ok;
}

bool EspOtaSink::activate() {
  if (!ready_ || !partition_) return false;
  if (esp_ota_set_boot_partition(partition_) != ESP_OK) return false;
  ready_ = false;
  return true; // Caller persists status, sends ACK, then explicitly reboots.
}

void EspOtaSink::abort() {
  if (writing_) esp_ota_abort(handle_);
  writing_ = false;
  ready_ = false;
  handle_ = 0;
  partition_ = nullptr;
}

} // namespace exlink
