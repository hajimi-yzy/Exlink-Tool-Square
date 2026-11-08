#include "device_update.h"
#include <string.h>

namespace exlink {

UpdateReceiver::UpdateReceiver(UpdateSink &sink) : sink_(sink), kind_(UpdateKind::Firmware),
    lastOffset_(0), lastSize_(0), lastCrc_(0) {
  status_ = {UpdateState::Idle, UpdateResult::Ok, 0, 0, 0};
  memset(expectedHash_, 0, sizeof(expectedHash_));
}

bool UpdateReceiver::matches(const UpdateManifest &manifest) const {
  return (status_.state == UpdateState::Receiving || status_.state == UpdateState::Ready) &&
      manifest.kind == kind_ && manifest.session == status_.session &&
      manifest.size == status_.total &&
      memcmp(manifest.sha256, expectedHash_, sizeof(expectedHash_)) == 0;
}

UpdateResult UpdateReceiver::begin(const UpdateManifest &manifest,
                                   bool authorized, bool outputsQuiesced) {
  if (!authorized) return UpdateResult::Unauthorized;
  if (matches(manifest)) return UpdateResult::Ok;
  if (status_.state == UpdateState::Receiving || status_.state == UpdateState::Ready)
    return UpdateResult::Busy;
  if (!outputsQuiesced) return UpdateResult::Busy;
  if (manifest.session == 0 || manifest.size == 0 ||
      (manifest.kind != UpdateKind::Firmware && manifest.kind != UpdateKind::ScreenAsset))
    return UpdateResult::InvalidManifest;
  uint8_t hashBits = 0;
  for (uint8_t byte : manifest.sha256) hashBits |= byte;
  if (!hashBits) return UpdateResult::InvalidManifest;
  status_ = {UpdateState::Receiving, UpdateResult::Ok, manifest.session, 0, manifest.size};
  memcpy(expectedHash_, manifest.sha256, sizeof(expectedHash_));
  kind_ = manifest.kind;
  lastSize_ = 0;
  if (!sink_.begin(manifest)) return fail(UpdateResult::StorageError);
  return UpdateResult::Ok;
}

UpdateResult UpdateReceiver::chunk(uint32_t session, uint32_t offset,
                                   const uint8_t *data, size_t size, uint32_t crc) {
  if (status_.state != UpdateState::Receiving) return UpdateResult::NotReady;
  if (session != status_.session) return UpdateResult::WrongSession;
  // Stop-and-wait retransmission: only the immediately preceding verified
  // chunk may be acknowledged again. Never write it to flash a second time.
  if (offset != status_.accepted && (!lastSize_ || offset != lastOffset_))
    return UpdateResult::WrongOffset;
  if (!data || !size || size > MaxChunk ||
      crc32(data, size) != crc) return UpdateResult::BadChunk;
  if (offset != status_.accepted)
    return size == lastSize_ && crc == lastCrc_ ? UpdateResult::Ok : UpdateResult::BadChunk;
  if (size > status_.total - status_.accepted) return UpdateResult::BadChunk;
  if (!sink_.write(data, size)) return fail(UpdateResult::StorageError);
  lastOffset_ = offset;
  lastSize_ = size;
  lastCrc_ = crc;
  status_.accepted += static_cast<uint32_t>(size);
  return UpdateResult::Ok;
}

UpdateResult UpdateReceiver::finish(uint32_t session) {
  if (session != status_.session) return UpdateResult::WrongSession;
  if (status_.state == UpdateState::Ready) return UpdateResult::Ok;
  if (status_.state != UpdateState::Receiving || status_.accepted != status_.total)
    return UpdateResult::NotReady;
  UpdateResult result = sink_.finish(expectedHash_);
  if (result != UpdateResult::Ok) return fail(result);
  status_.state = UpdateState::Ready;
  return UpdateResult::Ok;
}

UpdateResult UpdateReceiver::activate(uint32_t session, bool authorized) {
  if (!authorized) return UpdateResult::Unauthorized;
  if (session != status_.session) return UpdateResult::WrongSession;
  if (status_.state != UpdateState::Ready) return UpdateResult::NotReady;
  if (!sink_.activate()) return fail(UpdateResult::StorageError);
  status_.state = UpdateState::Idle;
  return UpdateResult::Ok;
}

UpdateResult UpdateReceiver::fail(UpdateResult result) {
  sink_.abort();
  status_.state = UpdateState::Failed;
  status_.result = result;
  return result;
}

void UpdateReceiver::abort() {
  sink_.abort();
  status_ = {UpdateState::Idle, UpdateResult::Ok, 0, 0, 0};
  memset(expectedHash_, 0, sizeof(expectedHash_));
  lastSize_ = 0;
}

uint32_t UpdateReceiver::crc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return ~crc;
}

static uint32_t read32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

UpdateResult receiveDataFrame(UpdateReceiver &receiver, const uint8_t *frame, size_t size) {
  if (!frame || size < 15 || frame[0] != 2) return UpdateResult::BadChunk;
  size_t length = size_t(frame[9]) | size_t(frame[10]) << 8;
  if (length > UpdateReceiver::MaxChunk || size != 15 + length) return UpdateResult::BadChunk;
  return receiver.chunk(read32(frame + 1), read32(frame + 5), frame + 15,
                        length, read32(frame + 11));
}

} // namespace exlink
