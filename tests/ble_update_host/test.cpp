#include "ble_update_service.h"
#include "device_ota_sink.h"
#include <cassert>
#include <cstdio>

uint32_t testMillis = 1;
TestEsp ESP;
BLESecurityCallbacks *BLEDevice::security;
namespace exlink {
unsigned testSinkBegins, testSinkWrites, testSinkFinishes, testSinkActivations;
static bool consent, outputsHeld;
static unsigned reports;
bool updateQuiesceOutputs() { outputsHeld = true; return true; }
void updateRestoreOutputs() { outputsHeld = false; }
bool updateLocalConsent() { return consent; }
void updateRevokeConsent() { consent = false; }
size_t updateReadTelemetry(char *, size_t) { return 0; }
void updateReportProgress(const UpdateProgressSnapshot &) { ++reports; }
}
using namespace exlink;
static const uint8_t peerA[6] = {1, 2, 3, 4, 5, 6};
static const uint8_t peerB[6] = {9, 8, 7, 6, 5, 4};
static void connect(const uint8_t *peer, bool bonded = true, bool successful = true) {
  bleUpdateConnection(true, peer);
  esp_ble_auth_cmpl_t authentication{{0}, successful, uint8_t(bonded ? ESP_LE_AUTH_BOND : 0)};
  memcpy(authentication.bd_addr, peer, 6);
  BLEDevice::security->onAuthenticationComplete(authentication);
  bleUpdatePoll();
}
static void write32(std::vector<uint8_t> &bytes, size_t offset, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(n >> (8 * i));
}
static std::vector<uint8_t> manifest() {
  std::vector<uint8_t> bytes(42);
  bytes[0] = 1; bytes[1] = 1; bytes[10] = 1;
  write32(bytes, 2, 42); write32(bytes, 6, 3);
  return bytes;
}
static std::vector<uint8_t> data() {
  std::vector<uint8_t> bytes(18);
  bytes[0] = 2; bytes[9] = 3;
  bytes[15] = 1; bytes[16] = 2; bytes[17] = 3;
  write32(bytes, 1, 42);
  write32(bytes, 11, UpdateReceiver::crc32(bytes.data() + 15, 3));
  return bytes;
}
int main() {
  BLEServer server;
  bleUpdateInit(&server);
  bleUpdateSetEnabled(true);
  auto *control = server.service.characteristics.at("7c9e0002-7b3f-4f6d-9e1a-71b4c3a22100");
  auto *payload = server.service.characteristics.at("7c9e0003-7b3f-4f6d-9e1a-71b4c3a22100");
  auto *status = server.service.characteristics.at("7c9e0004-7b3f-4f6d-9e1a-71b4c3a22100");
  assert(status->permissions == ESP_GATT_PERM_READ_ENCRYPTED);
  connect(peerA);
  consent = true;
  control->write(manifest()); bleUpdatePoll();
  assert(bleUpdateBusy() && outputsHeld && testSinkBegins == 1);
  control->write(manifest()); bleUpdatePoll();
  assert(testSinkBegins == 1 && bleUpdateProgress().result == UpdateResult::Ok);

  // Disconnect must hold outputs and flash session, without requiring fresh
  // local consent. Another authenticated/bonded peer cannot read or resume it.
  bleUpdateConnection(false); bleUpdatePoll();
  assert(bleUpdateProgress().paused && outputsHeld && !consent);
  connect(peerB);
  assert(!BLEDevice::security->onSecurityRequest());
  payload->write(data()); bleUpdatePoll();
  assert(status->read().empty() && testSinkWrites == 0 && bleUpdateProgress().paused);
  connect(peerA, false);
  payload->write(data()); bleUpdatePoll();
  assert(testSinkWrites == 0 && bleUpdateProgress().paused && status->read().empty());
  connect(peerA, true, false);
  assert(bleUpdateProgress().paused);
  connect(peerA);
  assert(BLEDevice::security->onSecurityRequest());
  assert(!bleUpdateProgress().paused && bleUpdateBusy());
  assert(status->read().size() == 16);
  payload->write(data()); bleUpdatePoll();
  payload->write(data()); bleUpdatePoll();
  assert(testSinkWrites == 1 && bleUpdateProgress().accepted == 3);

  // Ready survives reconnect, and END is safe to retry after losing its ACK.
  control->write({3, 42, 0, 0, 0}); bleUpdatePoll();
  assert(bleUpdateProgress().state == UpdateState::Ready && testSinkFinishes == 1);
  bleUpdateConnection(false); bleUpdatePoll();
  connect(peerA);
  control->write({3, 42, 0, 0, 0}); bleUpdatePoll();
  assert(testSinkFinishes == 1 && bleUpdateProgress().result == UpdateResult::Ok);
  control->write({4, 42, 0, 0, 0}); bleUpdatePoll();
  auto progress = bleUpdateProgress();
  assert(testSinkActivations == 1 && progress.state == UpdateState::Idle && progress.session == 42);
  assert(progress.accepted == 3 && progress.total == 3);

  // Even a quick disconnect/reconnect cannot execute a write queued by the
  // preceding connection. Only a fresh retry from the owner is accepted.
  consent = true;
  control->write(manifest()); bleUpdatePoll();
  payload->write(data());
  bleUpdateConnection(false);
  connect(peerA);
  assert(testSinkWrites == 1 && bleUpdateProgress().accepted == 0);
  payload->write(data()); bleUpdatePoll();
  assert(testSinkWrites == 2);

  // Repeated unrelated connections do not extend the original 60s lease.
  bleUpdateConnection(false); bleUpdatePoll();
  testMillis += 59000;
  connect(peerB);
  bleUpdateConnection(false); bleUpdatePoll();
  testMillis += 1000;
  connect(peerA);
  assert(!bleUpdateBusy() && !outputsHeld && bleUpdateProgress().state == UpdateState::Idle);
  assert(bleUpdateProgress().result == UpdateResult::NotReady);

  consent = true;
  control->write(manifest()); bleUpdatePoll();
  bleUpdateConnection(false); bleUpdatePoll();
  bleUpdateSetEnabled(false);
  assert(!bleUpdateBusy() && !outputsHeld && !bleUpdateProgress().paused);
  bleUpdateShutdown();
  assert(reports > 0);
  puts("BLE update host tests PASS (encrypted status/owner/bond/resume/Ready/timeout/generation/disable)");
}
