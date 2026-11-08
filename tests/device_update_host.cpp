#include "device_update.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace exlink;
struct Sink : UpdateSink {
  unsigned begins = 0, writes = 0, finishes = 0, activations = 0;
  bool hashFail = false;
  bool begin(const UpdateManifest &) override { ++begins; return true; }
  bool write(const uint8_t *, size_t) override { ++writes; return true; }
  UpdateResult finish(const uint8_t *) override { ++finishes; return hashFail ? UpdateResult::HashMismatch : UpdateResult::Ok; }
  bool activate() override { ++activations; return true; }
  void abort() override {}
};
int main() {
  Sink sink; UpdateReceiver receiver(sink);
  UpdateManifest manifest{UpdateKind::Firmware, 42, 3, {1}};
  auto zero = manifest; zero.session = 0;
  assert(receiver.begin(zero, true, true) == UpdateResult::InvalidManifest);
  assert(receiver.begin(manifest, false, true) == UpdateResult::Unauthorized);
  assert(receiver.begin(manifest, true, false) == UpdateResult::Busy);
  assert(receiver.begin(manifest, true, true) == UpdateResult::Ok);
  assert(receiver.begin(manifest, true, true) == UpdateResult::Ok);
  assert(sink.begins == 1);
  auto changed = manifest; changed.sha256[1] = 1;
  assert(receiver.begin(changed, true, true) == UpdateResult::Busy);
  changed = manifest; changed.kind = UpdateKind::ScreenAsset;
  assert(receiver.begin(changed, true, true) == UpdateResult::Busy);
  changed = manifest; ++changed.size;
  assert(receiver.begin(changed, true, true) == UpdateResult::Busy);
  changed = manifest; ++changed.session;
  assert(receiver.begin(changed, true, true) == UpdateResult::Busy);
  assert(receiver.activate(42, true) == UpdateResult::NotReady);
  uint8_t bytes[]{1, 2, 3}; uint32_t crc = UpdateReceiver::crc32(bytes, 3);
  assert(receiver.chunk(41, 0, bytes, 3, crc) == UpdateResult::WrongSession);
  assert(receiver.chunk(42, 1, bytes, 3, crc) == UpdateResult::WrongOffset);
  assert(receiver.chunk(42, 0, bytes, 3, crc ^ 1) == UpdateResult::BadChunk);
  assert(receiver.chunk(42, 0, bytes, 481, crc) == UpdateResult::BadChunk);
  assert(receiver.chunk(42, 0, bytes, 0, crc) == UpdateResult::BadChunk);
  assert(receiver.finish(42) == UpdateResult::NotReady);
  uint8_t frame[18] = {2,42,0,0,0,0,0,0,0,3,0};
  for (unsigned i=0;i<4;++i) frame[11+i]=uint8_t(crc>>(i*8));
  memcpy(frame+15,bytes,3);
  assert(receiveDataFrame(receiver, frame, 17) == UpdateResult::BadChunk);
  frame[9]=4;
  assert(receiveDataFrame(receiver, frame, 18) == UpdateResult::BadChunk);
  frame[9]=3;
  assert(sink.writes == 0 && receiver.status().accepted == 0);
  assert(receiveDataFrame(receiver, frame, 18) == UpdateResult::Ok);
  assert(receiver.status().accepted == 3 && sink.writes == 1);
  // Lost ACK: an identical validated DATA retry must not write again.
  assert(receiveDataFrame(receiver, frame, 18) == UpdateResult::Ok);
  assert(receiver.status().accepted == 3 && sink.writes == 1);
  uint8_t changedBytes[]{1, 2, 4};
  assert(receiver.chunk(42, 0, changedBytes, 3, crc) == UpdateResult::BadChunk);
  assert(receiver.chunk(42, 0, changedBytes, 3, UpdateReceiver::crc32(changedBytes, 3)) == UpdateResult::BadChunk);
  assert(receiver.chunk(42, 0, bytes, 2, UpdateReceiver::crc32(bytes, 2)) == UpdateResult::BadChunk);
  assert(receiver.chunk(41, 0, bytes, 3, crc) == UpdateResult::WrongSession);
  assert(sink.writes == 1);
  assert(receiver.activate(42,true) == UpdateResult::NotReady);
  sink.hashFail=true;
  assert(receiver.finish(42) == UpdateResult::HashMismatch);
  assert(receiver.activate(42,true) == UpdateResult::NotReady);
  assert(sink.activations == 0);
  receiver.abort(); sink.hashFail=false;
  assert(receiver.begin(manifest,true,true) == UpdateResult::Ok);
  assert(receiver.chunk(42,0,bytes,3,crc) == UpdateResult::Ok);
  assert(receiver.finish(41) == UpdateResult::WrongSession);
  assert(receiver.finish(42) == UpdateResult::Ok);
  const unsigned finishes = sink.finishes;
  assert(receiver.finish(42) == UpdateResult::Ok);
  assert(receiver.begin(manifest, true, true) == UpdateResult::Ok);
  assert(receiver.begin(manifest, false, true) == UpdateResult::Unauthorized);
  assert(sink.finishes == finishes);
  assert(receiver.activate(42,false) == UpdateResult::Unauthorized);
  assert(receiver.activate(41,true) == UpdateResult::WrongSession);
  assert(receiver.activate(42,true) == UpdateResult::Ok);
  assert(sink.activations == 1 && receiver.status().state == UpdateState::Idle);
  assert(receiver.status().session == 42 && receiver.status().accepted == 3 && receiver.status().total == 3);
  receiver.abort();
  manifest.size = 9;
  assert(receiver.begin(manifest, true, true) == UpdateResult::Ok);
  assert(receiver.chunk(42, 0, bytes, 3, crc) == UpdateResult::Ok);
  assert(receiver.chunk(42, 3, bytes, 3, crc) == UpdateResult::Ok);
  const unsigned writes = sink.writes;
  assert(receiver.chunk(42, 0, bytes, 3, crc) == UpdateResult::WrongOffset);
  assert(receiver.chunk(42, 3, bytes, 3, crc) == UpdateResult::Ok);
  assert(receiver.status().accepted == 6 && sink.writes == writes);
  assert(receiver.chunk(42, 6, bytes, 3, crc) == UpdateResult::Ok);
  receiver.abort();
  assert(receiver.begin(manifest, true, true) == UpdateResult::Ok);
  assert(receiver.chunk(42, 6, bytes, 3, crc) == UpdateResult::WrongOffset);
  assert(UpdateReceiver::crc32(reinterpret_cast<const uint8_t *>("123456789"),9)==0xCBF43926U);
  puts("UpdateReceiver host tests PASS (CRC/session/offset/frame/hash/activation/idempotent BEGIN-DATA-END)");
}
