#pragma once

#include "device_update.h"
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

namespace exlink {

// Writes only the inactive OTA app slot. No global instance or runtime hookup.
class EspOtaSink : public UpdateSink {
public:
  EspOtaSink();
  ~EspOtaSink() override;
  bool begin(const UpdateManifest &manifest) override;
  bool write(const uint8_t *data, size_t size) override;
  UpdateResult finish(const uint8_t sha256[32]) override;
  bool activate() override;
  void abort() override;
private:
  const esp_partition_t *partition_;
  esp_ota_handle_t handle_;
  mbedtls_sha256_context hash_;
  bool writing_;
  bool ready_;
};

} // namespace exlink
