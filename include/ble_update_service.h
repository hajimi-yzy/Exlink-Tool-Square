#pragma once
#include <BLEServer.h>
#include "device_update.h"

namespace exlink {
// Call init after the existing BLE server is created, then advertise service UUID.
void bleUpdateInit(BLEServer *server);
void bleUpdatePoll();
void bleUpdateConnection(bool connected, const uint8_t *identity = nullptr);
void bleUpdateSetEnabled(bool enabled);
bool bleUpdateTakeActivity();
bool bleUpdateBusy();
void bleUpdateShutdown();
struct UpdateProgressSnapshot {
  UpdateKind kind;
  UpdateState state;
  UpdateResult result;
  uint32_t session;
  uint32_t accepted;
  uint32_t total;
  bool active;
  bool paused;
};
// Read and report only in setup/loop, never BTC_TASK. The optional UI hook is
// called after status changes, including a disconnect/resume or timeout.
UpdateProgressSnapshot bleUpdateProgress();
void updateReportProgress(const UpdateProgressSnapshot &progress);
// Business/output hooks execute in setup/loop, never a BLE callback.
bool updateQuiesceOutputs();
void updateRestoreOutputs();
bool updateLocalConsent();
void updateRevokeConsent();
size_t updateReadTelemetry(char *buffer, size_t capacity);
// Only the bounded log ring drain is permitted inside the GATT read callback.
size_t updateReadLog(char *buffer, size_t capacity);
UpdateSink *screenAssetUpdateSink();
bool screenAssetRestoreDefault();
}
