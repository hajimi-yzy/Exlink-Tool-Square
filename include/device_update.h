#pragma once

#include <stddef.h>
#include <stdint.h>

namespace exlink {

// Transport-independent receiver. BLE callbacks must queue packets; call this
// receiver only from one worker/loop context, never from the BLE callback.
enum class UpdateKind : uint8_t { Firmware = 1, ScreenAsset = 2 };
enum class UpdateState : uint8_t { Idle, Receiving, Ready, Failed };
enum class UpdateResult : uint8_t {
  Ok, Unauthorized, Busy, InvalidManifest, WrongSession, WrongOffset,
  BadChunk, StorageError, HashMismatch, NotReady
};

struct UpdateManifest {
  UpdateKind kind;
  uint32_t session;
  uint32_t size;
  uint8_t sha256[32];
};

struct UpdateStatus {
  UpdateState state;
  UpdateResult result;
  uint32_t session;
  uint32_t accepted;
  uint32_t total;
};

class UpdateSink {
public:
  virtual ~UpdateSink() {}
  virtual bool begin(const UpdateManifest &manifest) = 0;
  virtual bool write(const uint8_t *data, size_t size) = 0;
  // Verify digest and payload format; do not select boot/active resource yet.
  virtual UpdateResult finish(const uint8_t sha256[32]) = 0;
  virtual bool activate() = 0;
  virtual void abort() = 0;
};

class UpdateReceiver {
public:
  static constexpr size_t MaxChunk = 480;
  explicit UpdateReceiver(UpdateSink &sink);
  UpdateResult begin(const UpdateManifest &manifest, bool authorized,
                     bool outputsQuiesced);
  UpdateResult chunk(uint32_t session, uint32_t offset, const uint8_t *data,
                     size_t size, uint32_t crc32);
  UpdateResult finish(uint32_t session);
  UpdateResult activate(uint32_t session, bool authorized);
  void abort();
  UpdateStatus status() const { return status_; }
  bool matches(const UpdateManifest &manifest) const;
  static uint32_t crc32(const uint8_t *data, size_t size);
private:
  UpdateResult fail(UpdateResult result);
  UpdateSink &sink_;
  UpdateStatus status_;
  uint8_t expectedHash_[32];
  UpdateKind kind_;
  uint32_t lastOffset_;
  size_t lastSize_;
  uint32_t lastCrc_;
};

// Strict frame parser: LE session/offset, CRC-32/ISO-HDLC over payload only.
// DATA frame: 0x02, session:u32, offset:u32, length:u16, crc:u32, payload.
UpdateResult receiveDataFrame(UpdateReceiver &receiver, const uint8_t *frame,
                              size_t size);

} // namespace exlink
