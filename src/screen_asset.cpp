#include "screen_asset.h"
#include "app_log.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace exlink {
namespace {
constexpr uint32_t kMaxSize = 0x380000;
constexpr uint32_t kRecordMagic = 0x31415345; // ESA1
constexpr uint32_t kBuiltin = 2;
// A single NVS blob is the activation commit. Previous is written first so a
// reset during activation leaves either the old or the new complete record.
struct Record {
  uint32_t magic;
  uint32_t slot;
  uint32_t size;
  uint8_t sha256[32];
};
static_assert(sizeof(Record) == 44, "NVS resource record layout");

const esp_partition_t *findSlot(uint32_t slot) {
  if (slot > 1) return nullptr;
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      static_cast<esp_partition_subtype_t>(0x40), slot ? "assets_b" : "assets_a");
}

bool readRecord(Preferences &prefs, const char *key, Record &record) {
  if (prefs.getBytesLength(key) != sizeof(record) ||
      prefs.getBytes(key, &record, sizeof(record)) != sizeof(record)) return false;
  return record.magic == kRecordMagic &&
      ((record.slot <= 1 && record.size >= 32 && record.size <= kMaxSize) ||
       (record.slot == kBuiltin && record.size == 0));
}

bool equalHash(const uint8_t *a, const uint8_t *b) {
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}

bool flashHash(const uint8_t *bytes, uint32_t size, const uint8_t expected[32]) {
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0;
  for (uint32_t offset = 0; ok && offset < size; ) {
    const uint32_t count = size - offset < 4096 ? size - offset : 4096;
    ok = mbedtls_sha256_update_ret(&sha, bytes + offset, count) == 0;
    offset += count;
    delay(0);
  }
  uint8_t actual[32];
  ok = ok && mbedtls_sha256_finish_ret(&sha, actual) == 0;
  mbedtls_sha256_free(&sha);
  return ok && equalHash(actual, expected);
}

struct Mapping {
  spi_flash_mmap_handle_t handle = 0;
  bool mapped = false;
  exa::View view{};
  void close() {
    if (mapped) spi_flash_munmap(handle);
    mapped = false;
    view = exa::View{};
  }
  bool open(const Record &record) {
    close();
    const esp_partition_t *part = findSlot(record.slot);
    const void *bytes = nullptr;
    if (!part || record.size > part->size ||
        esp_partition_mmap(part, 0, record.size, SPI_FLASH_MMAP_DATA,
                           &bytes, &handle) != ESP_OK) return false;
    mapped = true;
    // Re-read the stored Flash, not just the stream that reached the sink.
    if (!flashHash(static_cast<const uint8_t *>(bytes), record.size, record.sha256) ||
        !exa::validate(static_cast<const uint8_t *>(bytes), record.size, &view)) {
      close();
      return false;
    }
    return true;
  }
};

Mapping activeMap;
bool cacheLoaded = false;
bool haveSelected = false;
Record selected{};
bool havePrevious = false;
Record previous{};

void invalidateCache() {
  activeMap.close();
  cacheLoaded = haveSelected = havePrevious = false;
}

void loadCache() {
  if (cacheLoaded) return;
  cacheLoaded = true;
  Preferences prefs;
  if (!prefs.begin("screen", true)) return;
  Record active{};
  const bool haveActive = readRecord(prefs, "active", active);
  havePrevious = readRecord(prefs, "previous", previous);
  prefs.end();
  if (haveActive && active.slot == kBuiltin) {
    selected = active;
    haveSelected = true;
    return; // Deliberately restored defaults must not select the old upload.
  }
  if (haveActive && activeMap.open(active)) {
    selected = active;
    haveSelected = true;
    return;
  }
  if (havePrevious && previous.slot <= 1 && activeMap.open(previous)) {
    selected = previous;
    haveSelected = true;
    APP_LOGW("asset", "active resource invalid; using previous upload\r\n");
  } else if (haveActive) {
    APP_LOGW("asset", "resource invalid; using built-in expressions\r\n");
  }
}

bool commitRecord(const Record &next) {
  loadCache();
  Preferences prefs;
  if (!prefs.begin("screen", false)) return false;
  bool ok = true;
  if (haveSelected && selected.slot <= 1) {
    ok = prefs.putBytes("previous", &selected, sizeof(selected)) == sizeof(selected);
  }
  if (ok) ok = prefs.putBytes("active", &next, sizeof(next)) == sizeof(next);
  prefs.end();
  if (ok) invalidateCache();
  return ok;
}

class AssetSink final : public UpdateSink {
public:
  AssetSink() { mbedtls_sha256_init(&sha_); }
  ~AssetSink() override { mbedtls_sha256_free(&sha_); }
  bool begin(const UpdateManifest &manifest) override {
    abort();
    if (manifest.kind != UpdateKind::ScreenAsset || manifest.size < 32 ||
        manifest.size > kMaxSize) return false;
    loadCache();
    // Keep the selected resource intact, including the most recent upload
    // when the user has temporarily switched back to built-in expressions.
    uint32_t preserved = haveSelected && selected.slot <= 1 ? selected.slot : kBuiltin;
    if (preserved == kBuiltin && havePrevious && previous.slot <= 1)
      preserved = previous.slot;
    candidate_ = Record{};
    candidate_.magic = kRecordMagic;
    candidate_.slot = preserved == 0 ? 1 : 0;
    candidate_.size = manifest.size;
    memcpy(candidate_.sha256, manifest.sha256, sizeof(candidate_.sha256));
    part_ = findSlot(candidate_.slot);
    const uint32_t eraseSize = (manifest.size + 4095U) & ~4095U;
    if (!part_ || eraseSize > part_->size ||
        esp_partition_erase_range(part_, 0, eraseSize) != ESP_OK ||
        mbedtls_sha256_starts_ret(&sha_, 0) != 0) {
      abort();
      return false;
    }
    writing_ = true;
    return true;
  }
  bool write(const uint8_t *data, size_t size) override {
    if (!writing_ || !data || !size || size > candidate_.size - written_) return false;
    if (esp_partition_write(part_, written_, data, size) != ESP_OK ||
        mbedtls_sha256_update_ret(&sha_, data, size) != 0) return false;
    written_ += static_cast<uint32_t>(size);
    return true;
  }
  UpdateResult finish(const uint8_t expected[32]) override {
    if (!writing_ || written_ != candidate_.size) return UpdateResult::NotReady;
    writing_ = false;
    uint8_t actual[32];
    if (mbedtls_sha256_finish_ret(&sha_, actual) != 0) return UpdateResult::StorageError;
    if (!equalHash(actual, expected) || !equalHash(expected, candidate_.sha256))
      return UpdateResult::HashMismatch;
    Mapping check;
    const bool valid = check.open(candidate_);
    check.close();
    if (!valid) return UpdateResult::InvalidManifest;
    ready_ = true;
    return UpdateResult::Ok;
  }
  bool activate() override {
    if (!ready_ || !commitRecord(candidate_)) return false;
    ready_ = false;
    return true;
  }
  void abort() override {
    writing_ = ready_ = false;
    part_ = nullptr;
    written_ = 0;
    candidate_ = Record{};
    mbedtls_sha256_free(&sha_);
    mbedtls_sha256_init(&sha_);
  }
private:
  const esp_partition_t *part_ = nullptr;
  Record candidate_{};
  uint32_t written_ = 0;
  bool writing_ = false, ready_ = false;
  mbedtls_sha256_context sha_;
};
AssetSink sink;
} // namespace

UpdateSink *screenAssetUpdateSink() { return &sink; }
bool screenAssetRestoreDefault() {
  const Record builtin{kRecordMagic, kBuiltin, 0, {0}};
  return commitRecord(builtin);
}
bool screenAssetAvailable() { loadCache(); return activeMap.mapped; }
uint32_t screenAssetDurationMs() {
  return screenAssetAvailable() ? activeMap.view.durationMs : 0;
}
uint16_t screenAssetFrameCount() {
  return screenAssetAvailable() ? activeMap.view.frameCount : 0;
}
uint16_t screenAssetFrameForElapsed(uint32_t elapsedMs) {
  return screenAssetAvailable() ? exa::frameForElapsed(activeMap.view, elapsedMs) : 0;
}
bool screenAssetDecodeFrameRows(uint16_t frame, uint16_t *row, size_t capacity,
                               exa::RowCallback callback, void *context) {
  return screenAssetAvailable() &&
      exa::decodeFrameRows(activeMap.view, frame, row, capacity, callback, context);
}
} // namespace exlink
